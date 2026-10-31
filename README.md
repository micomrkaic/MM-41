# MM-41 — a modular RPN calculator

MM-41 is a programmable, stack-based RPN calculator for the terminal, written in
C17 on top of the GNU Scientific Library and GNU Readline. It handles **real,
complex and exact decimal scalars, strings, and real and complex matrices**; it
is programmable with user-defined words (conditionals, loops, recursion),
numerical methods that take any word as the function, and HP-41 style programs;
it has a full **finance module** (time value of money, amortisation, NPV/IRR,
bonds, depreciation, day counts) computing in 34-digit decimals; and, like the
HP-41 it is named after, it is built from **modules** you can add to.

It unifies the earlier MM-11, MM-14, MM-15 and MM-B1 calculators.

```text
$ mm41
MM-41>> 2 3 + 4 *
[0] ℝ : 20.000000
MM-41>> (1,2) (3,-1) *
[1] ℂ : (5.000000, 5.000000i)
MM-41>> [2 2 $ 4 7 2 6] det
[2] ℝ : 10.000000
MM-41>> : f dup * 2 - ;
MM-41>> 0 3 @f fzero
[3] ℝ : 1.414214
MM-41>> 0.1d 0.2d +
[4] 𝔻 : 0.300000
MM-41>> 2 setprec clrtvm 360 n 0.5 i% 300000 pv 0 fv ?pmt
[5] 𝔻 : -1798.65
MM-41>> modules
module       version  words  summary
core         1.1         85  arithmetic, functions, logic, the stack, words and the session
decimal      1.0          7  decimal numbers for money: number mode, conversions, rounding
finance      1.0         33  time value of money, cash flows, depreciation, bonds, day counts
complex      1.0          7  complex numbers: parts, argument, conjugate, construction
special      1.0          7  gamma, beta and the standard normal distribution
matrices     1.0         38  linear algebra, matrix construction and reduction, polynomials
numerics     1.0         12  integration, roots, minima, derivatives and sums over any word
strings      1.0          7  string construction and manipulation
registers    1.0          7  64 storage registers of any type
dates        1.0         13  calendar arithmetic, time zones, sunrise and sunset
programs     1.0          1  HP-41 style programs with labels, jumps and subroutines
std          1.0         28  standard word module: powers, norms, storage arithmetic, locations
```

The **[user manual](doc/mm41-manual.pdf)** (PDF) covers everything
with a complete command reference. `man mm41` and [COMMANDS.md](COMMANDS.md)
are the short forms; inside the calculator, `help`, `listfcns`, `modules` and
`"name" usage`.

## Modules

Every command belongs to a module, and there are three kinds — indistinguishable
from the stack:

- **Built-in modules** live in `modules/<name>/` and are compiled in.
  `make MODULES="matrices strings"` builds a smaller calculator.
- **Word modules** are text files of definitions. Drop `geo.mm` into
  `~/.config/mm41/modules/` and it is loaded at every start; `"geo" use` and
  `"geo" unuse` load and unload by hand. The shipped `std` module is one.
  ```text
  \ module geo 0.1 circles and spheres
  : circ 2 * pi * ;
  sphere dup * 4 * pi *
  ```
- **Compiled modules** are shared objects exporting `mm41_module()` — the
  same `Module` struct the built-ins use (`include/module.h`).
  `make shared-module DIR=examples/modules/hello` builds the example;
  `"hello.so" load` loads it, or put it in `~/.config/mm41/modules/`.

Two modules may not define the same name; the clash is reported when the second
loads. `"name" usage` tells you which module a command comes from.

## What the core and the shipped modules do

- **Atomic errors.** Every token either succeeds or leaves the stack exactly as
  it was; the error is printed and the rest of the line is skipped. `undo`
  restores the previous line's stack.
- **Two promotion rules.** `-1 sqrt` is `(0,1)`, `2 asin` is `(1.5708,1.3170)`,
  `-8 3 inv ^` is `(1,1.7321)`; a real matrix with one such entry becomes a
  complex matrix. Division by zero is an error, not `inf`. And decimal absorbs
  binary: `0.1d 0.2 +` is exactly `0.3`.
- **Decimals** (IBM decNumber, 34 digits, round-half-even): the suffix `d` or
  the `decimal` mode makes literals exact; `>dec`, `>bin`, `round`; the
  arithmetic, percentages, comparisons and registers understand them; anything
  that would lose digits (`>bin`) is explicit.
- **Finance**, MM-B1 as a module, all in decimals: TVM registers `n i% pv pmt
  fv` with `?n ?i% ?pv ?pmt ?fv`, `beg`/`end`, `amort`/`amortm`; `npv`/`irr`
  over cash-flow vectors; `nom>eff`, `eff>nom`; `sl`, `soyd`, `db`; `bprice`,
  `byield`; dated `xnpv`, `xirr`, `xprice`, `xyield`, `xaccr`, `yearfrac` with
  Excel's five day-count bases.
- **Matrices** via GSL: products, elementwise operators, inverse, pseudoinverse,
  determinant, eigen-, Cholesky and singular value decompositions, Kronecker
  product, reshaping and joining, column/row statistics, polynomial roots and
  evaluation. Real and complex matrices mix.
- **Words**, compiled at definition time, with `if/else/then`, `begin/until`,
  `begin/while/repeat`, `do/loop/+loop` and `i`/`j`; recursion allowed.
- **Numerical methods** over any word or command: `integrate` (tanh-sinh),
  `romberg`, `fzero`, `solve`, `minimize`, `deriv`, `fsum`, `fprod`.
- **Strings, registers, dates and the sun**: 64 registers of any type; date
  arithmetic; sunrise, sunset and civil dawn/dusk with IANA time zones.
- **HP-41 style programs** from files (`LBL`, `GOTO`, `GOSUB`, `RTN`, tests).
- **Batch mode**: `-e EXPR`, `-f FILE`, or piped stdin; exit status 1 on any
  error.

## A taste

```text
: fac dup 1 gt if dup 1 - fac * then ;
10 fac                                   \ 3628800
0 pi @sin integrate                      \ 2
3 @sin solve                             \ 3.141592653590
[2 2 $ 4 7 2 6] minv pm                  \ | 0.6 -0.7 | / | -0.2 0.4 |
[1 3 $ 1 -3 2] roots pm                  \ | (1,0) (2,0) |
decimal 19.99 3 * 0.0825 1 + * 2 round   \ 64.92   exact, half-even
[5 1 $ -5000 1500 1500 1500 1500] irr    \ 7.713847  (percent per period)
20080215 20171115 5.75 6.5 100 2 0 xprice  \ 94.6344  (Excel's PRICE example)
"1.1.2025" "31.12.2025" ddays            \ 364
"21.6.2026" 46.24 14.36 "Europe/Ljubljana" sunset     \ "20:58"
```

```sh
mm41 -e "2 10 ^"                         # batch: prints the final stack, exits 1 on error
echo "[2 2 $ 1 2 3 4] det" | mm41 -n     # -n: ignore saved words, modules and config
```

## Input in one paragraph

Tokens are separated by whitespace and all input is case sensitive. Numbers:
`3`, `-1.2e-4`, `.5`; `0.1d` is a decimal, `2b` a binary double. Complex: `(1,2)`. Strings: `"text"` with `\" \\ \n \t`.
Matrices: `[rows cols $ elements...]`, or `[rows, cols, "file"]` to read one
from a file. `\` and `#` start comments. `@name` pushes an execution token for
the numerical methods. A line starting with `!` runs a shell command and pushes
its output.

## Build, test, install

Requires a C17 compiler, GNU make, GNU Readline and GSL
(`apt install build-essential libreadline-dev libgsl-dev`, or
`brew install gsl readline`).

```sh
make                 # release build in bin/release/
make test            # .t cases + golden files, under AddressSanitizer
make install         # ~/.local: bin/mm41, man page, manual, docs; modules into ~/.config/mm41
make uninstall
make MODE=asan       # or ubsan, harden
make MODULES="..."   # choose the built-in modules
make shared-module DIR=path/to/module
make docs            # regenerate COMMANDS.md and doc/commands.tex from the dispatch table
make manual          # rebuild doc/mm41-manual.pdf (needs LuaLaTeX; LATEX=xelatex also works)
```

`PREFIX=/usr/local make install` installs system-wide. Configuration, saved
words, modules and registers live in `$XDG_CONFIG_HOME/mm41` (default
`~/.config/mm41`).

## Files

| | |
|---|---|
| `src/`, `include/` | the core: lexer, stack, types, words, errors, the module registry |
| `modules/<name>/` | the built-in modules, one directory each |
| `vendor/decNumber/` | IBM's decNumber library (ICU licence), the decimal arithmetic |
| `examples/modules/hello/` | a complete compiled module |
| `data/` | default `config.txt`, the `std` word module, sample programs |
| `doc/manual.tex` → `doc/mm41-manual.pdf` | the user manual |
| `mm41.1`, `MANUAL.md`, `COMMANDS.md`, `CHANGES.md` | man page, quick reference, generated command list, changelog |
| `tests/` | unit cases and golden files |

MM-41 is free software under the GNU GPL, version 3 or later.
