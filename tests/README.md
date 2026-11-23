# MM-41 test harness

Run it:

```
make test
```

That builds the ASan binary (`make MODE=asan all`) and runs every case in
`tests/cases/*.t` against it. Exit status is nonzero if anything fails, so
it works in CI. Because it tests the ASan build, **every functional test
also gets memory-leak and undefined-behavior checking for free** — a wrong
answer *or* a leak fails the relevant case.

To run against a different build:

```
make MODE=ubsan all && BIN=bin/ubsan/mm41 bash tests/run_tests.sh
```

## Why it works the way it does

The calculator is a REPL that prints a splash screen and re-prints the
entire stack after every command. Diffing raw transcripts would be
hopelessly brittle — any precision tweak or extra stack line would break
every test at once. So instead each case ends with an automatically
appended `print`, which emits one recognizable line. The harness extracts
the final value from that line and compares it to the expected value with
a floating-point tolerance. This isolates "is the math right" from
cosmetic output noise.

Tests run with `HOME` redirected to a throwaway sandbox, so they never
read or write your real `~/.config/mm41` and never load your personal
macros/words (which would make results machine-dependent).

## Writing a test case

Create `tests/cases/<name>.t`:

```
# any line starting with # is a comment
#EXPECT: 42            <- required: the expected final value
#TOL: 1e-6             <- optional: abs tolerance for floats (default 1e-9)
6 7 *                  <- calculator input, one or more lines
```

Do **not** put `print` or `q` in the case — the harness appends them.
`#EXPECT:` is compared numerically when both sides look like numbers,
otherwise as an exact string (for string/complex results).

### Known-leak cases

If a case exercises a code path with a *tracked, known* leak that hasn't
been fixed yet, mark it:

```
#ALLOW_LEAK: short reason / tracking note
```

The case still verifies the numeric result and still prints a loud `WARN`
line every run (so the bug stays visible), but the leak alone won't fail
the suite. **Hard memory errors (use-after-free, buffer overflow, UB) are
never allowed and always fail, even with `#ALLOW_LEAK`.** When the
underlying bug is fixed, delete the `#ALLOW_LEAK` line and the case
becomes a regression guard for the fix.

## The other suites

`make test` also runs

- `tests/run_golden.sh`: every `tests/golden/NAME.rpn` in batch mode, its
  whole output compared with `NAME.out` (`UPDATE=1` regenerates them;
  read the diff). `robust.rpn` holds the inputs that once crashed or hung
  the calculator: `nan` as an index, a matrix of minus one rows, a date
  1e300 days away.
- `tests/run_scripts.sh`: scripts run from the shell, with arguments.
- `mm41 --tutor-check`: every line of the tutorial against a fresh stack.

## Fuzzing: `make fuzz`

`tests/fuzz.py` writes sessions nobody would type and runs them under the
sanitizers (leak detection on). It reads the command table from `mm41
--commands`, so a new command is fuzzed from the day it is added: each
line is a command's own example, a damaged copy of it, or random operands
(`nan`, `1e300`, empty strings, 300-character strings, matrices with zero
or a million rows) followed by random commands. A failure is reduced to
the shortest session that still fails in the same place and written to
`build/fuzz/`.

```
python3 tests/fuzz.py -n 4000            # sessions
python3 tests/fuzz.py -n 4000 --files    # damage the files the calculator reads back
python3 tests/fuzz.py --seed 12345 -n 1  # one session again
```

With `--files` the session is fixed and the files vary: the saved stack,
the registers, the words, the config, a program, a table, a holiday list
are written by the calculator, one is cut short, has bytes flipped or
numbers replaced, and everything is loaded again.

A single allocation is limited to 64 MB in these runs, so a session that
asks for a 20-million-row matrix exercises the out-of-memory path instead
of spending a minute on it.

## Out of memory: `make nomem`

`tests/nomem.py` runs every command's example with some things on the
stack and in the registers, once for each allocation the example makes,
with that allocation failed (`tests/nomem_shim.c`, loaded with
`LD_PRELOAD`). The command must be abandoned with an error, the stack must
be what it was, and using the stack and the registers afterwards must not
touch freed memory. `--scope all` also fails the interpreter's own
allocations (the snapshots it takes before each token); `--run 1000000`
makes every allocation from the chosen one on fail.

How the calculator survives is described in `include/errors.h`: a recovery
point around every token, to which the GSL error handler and `xmalloc`
jump. The one rule it imposes on a command is in that comment too: never
leave a freed pointer in the stack or in a register while allocating.
This test is what checks it.
