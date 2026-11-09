# MM-41 changes

## MM-41 1.4

The HP-41 application pacs that were still missing, as modules.

- **stat** (built in): `mean sd sem median skew kurt`, `quantile`, `cov`,
  `corr`, `movavg`, `hist`; the Advantage Pac's curve fits `linfit expfit
  logfit powfit` (coefficients and R² as a row vector); multiple regression
  `ols` (intercept added) and `olstab` (coefficient, standard error, t, p,
  and R²); the t, chi-square, F, binomial and Poisson distributions
  (`tcdf tinv chi2cdf chi2inv fcdf finv binompdf binomcdf poispdf poiscdf`).
- **random** (built in): `seed`; one draw `rnorm runif rpois rexp`; n draws
  as a column `normrnd unifrnd poisrnd binomrnd exprnd trnd chi2rnd`;
  `resample` (bootstrap) and `shuffle`; `n @f sims` runs a word n times and
  collects what it leaves; `mcstats` summarises a vector.
- **ode** (built in): `y0 t0 t1 n @f odeint` and `y0 t0 t1 @f odesol`,
  adaptive RKF45 to `tol`; the word is `t y -- y'`, scalar or a system with
  y a row vector.
- **special**: `erf erfc gammap gammaq betainc digamma zeta besselj bessely
  besseli besselk airyai airybi ellk elle expint lambertw`, all through
  GSL's error-checked entry points.
- **const.mm**, a second shipped word module: the physical and astronomical
  constants (CODATA 2018, SI).
- Cash flows: a matrix with two columns is always amount and repeat count,
  1×2 included, so `[1 2 $ 1000 100] 0 npv` is 100 000; two single flows are
  the column `[2 1 $ 1000 100]`. (1.1–1.3 read a 1×2 as two flows.)
- Fixed-point display falls back to scientific notation for a real it could
  only show as 0.000000 or as a fifteen-digit integer (`h` is
  `6.626070e-34`, not `0.000000`).
- `help stat`, `help ode`; the help legend knows vectors, regressors, degrees
  of freedom and the fit results.

## MM-41 1.3

- **`help name`**: one command in full. The stack picture is unfolded into
  the entries the command takes, each with what it must be (a date string
  or YYYYMMDD, a cash-flow vector, a rate in percent, a whole number, ...),
  what it leaves, and the example, which is run on a scratch stack when it
  has no side effects so the result is shown too. `help module` and
  `help category` list a module or a category, `help word` shows a user
  word, and `help text` searches names and descriptions (`help holiday`).
  `help` followed by a name is a line form: the name is looked up, not
  executed, and the stack is left alone. `"name" usage` shows the same page.
  Concept pages introduce one part of the calculator before its listing:
  `help modules` (the three kinds and how to add one), `help words`,
  `help decimal`, `help finance`, `help workdays`, `help numerics`,
  `help syntax`.
- Stack pictures write multi-word results as one token (`mod(y,x)`,
  `lnΓ(x)`, `[A;B]`, `[A|B]`).

## MM-41 1.2

- **Workdays and holiday calendars** in the dates module: `d1 d2 workdays`
  (both ends inclusive, negative when reversed: Excel's NETWORKDAYS),
  `date n workdayplus` (WORKDAY), `date isworkday`. The holiday calendar is
  a list of dates kept for the session: `"us" usehol` (the eleven federal
  holidays with the Saturday->Friday / Sunday->Monday observance rule),
  `"si" usehol` (Slovenia's work-free days, Easter Monday included), or
  `"name" usehol` from `~/.config/mm41/holidays/name.txt` (one date per
  line, comments allowed; `make install` ships an annotated `example.txt`).
  `usehol` records the name in `config.txt` (`holidays = us`) and the
  calendar is loaded on first use; `holidays` and `year name yholidays` push
  a calendar as a vector of YYYYMMDD, `H sethol` installs any vector, `hol`
  shows it, `clrhol` removes it. A calendar that cannot be loaded is an
  error on every use rather than a silently wrong count.
- The calendar core (serial days, Hinnant's civil/days algorithm, Easter,
  the two date spellings) moved from the finance module to `src/calendar.c`
  and is shared by the dates and finance modules.
- A matrix whose entries are all whole numbers prints them as integers
  (`pm`), so date vectors and cash-flow tables are readable; other matrices
  print as before.
- **Fixed:** `config.txt` was written as `key = value` but read with the
  space left in the key, so no setting (precision, display mode, number
  mode, tolerance) ever survived a restart. Keys and values are trimmed.

## MM-41 1.1

MM-B1, the decimal business calculator, becomes two modules; with this
release MM-41 contains the whole MM family.

- **Decimal numbers**, a fifth stack type (glyph `𝔻`): IBM decNumber,
  vendored under `vendor/decNumber/`, 34 significant digits, round-half-even.
  The literal suffix `d` makes a decimal and `b` a binary double; the
  `decimal`/`binary` commands switch the number mode (prompt `MM-41[dec]>>`,
  saved as `number_mode` in `config.txt`). Decimal absorbs binary: a decimal
  combined with a real gives a decimal, the real converted by its shortest
  round-trip form. `+ - * / ^ chs abs inv sqrt exp ln log intg frac mod % %t
  %chg`, the comparisons and logic, `sto`/`rcl`, `savestack`/`loadstack`,
  `int2str` and the control words accept decimals; the binary-only parts
  (trigonometry, complex, matrices, numerics) reject a decimal value with a
  message naming `>bin`, while a decimal given as an index, count, dimension,
  precision or flag is converted. Module `decimal`: `decimal binary >dec >bin
  round decmode isdec`. Fixed-point display rounds to `setprec` places,
  scientific display shows the number as stored.
- **Finance module** (`modules/finance/`), MM-B1's kernel ported onto the
  decimal type; every word computes and returns decimals, rates in percent
  per period, cash received positive. TVM registers `n i% pv pmt fv`, solvers
  `?n ?i% ?pv ?pmt ?fv` (closed forms; the rate by bracketing and decimal
  bisection), `beg`/`end`, `clrtvm`, `tvm`; `amort` (printed schedule) and
  `amortm` (n×4 matrix); `npv`/`irr` over cash-flow vectors in time order
  (N×2 for repeat counts); `nom>eff`, `eff>nom`; `sl`, `soyd`, `db`;
  `bprice`, `byield`; dated `xnpv`, `xirr` (actual/365 from the first date),
  `xprice`, `xyield`, `xaccr` (SIA conventions, bases 0–4), `yearfrac`;
  dates as `"d.m.yyyy"` strings or `YYYYMMDD` numbers, converted by
  `date>ymd`/`ymd>date`. The `tests/golden/finance.rpn` cases reproduce
  MM-B1's regression values (and Excel's PRICE/YIELD/ACCRINT/XNPV/XIRR
  examples).
- The `std` macros `npv` and `irr` (latest flow first, rate as a fraction)
  are removed in favour of the finance words.
- In decimal mode a decimal is accepted wherever a command wants an index,
  count, dimension, precision or flag (`2 setprec`, `1 pick`, `5 rcl`,
  `3 3 eye`, `do ... loop` limits, the numerical methods' bounds).
- Fixes from a type sweep of every command under ASan: a matrix pushed onto
  a full stack was leaked; `reshape` with huge dimensions overflowed an
  `int`; `eye`, `ones`, `zeroes`, `rand` and `randn` now refuse more than
  50 million elements instead of asking GSL for terabytes. `pop` clears the
  vacated slot so the leak detector sees what is really leaked.
- Manual chapters *Decimal numbers* and *Finance*; the *Coming from* appendix
  maps every MM-B1 word to its MM-41 spelling.

## MM-41 1.0

MM-41 unifies the MM calculator family. It starts from MM-15 2.1 (which
already contained MM-11 and MM-14) and restructures it into modules; every
MM-15 command, word and program runs unchanged.

- **Modules.** `include/module.h` defines a module as a named, versioned set
  of commands with init/shutdown hooks. The core (stack, types, words,
  errors, batch) is the module `core`; complex, special, matrices (with
  polynomials), numerics, strings, registers, dates and programs are modules
  under `modules/<name>/`, compiled in according to `MODULES` in the
  Makefile. `modules` and `--modules` list them; `"name" usage`, `listfcns`
  and the generated reference name the module of each command. Two modules
  defining one name is an error at load time.
- **Word modules.** Files of definitions (`name body` or `: name body ;`
  lines, optional `\ module name version summary` header) in
  `~/.config/mm41/modules/*.mm` are loaded at startup; `"name" use` and
  `"name" unuse` load and unload by hand; `listmacros` lists them by module.
  `predefined_macros.txt` is now the `std` word module, installed as
  `modules/std.mm`.
- **Compiled modules.** A shared object exporting `mm41_module()` is loaded
  with `"file.so" load` or from the same directory at startup;
  `make shared-module DIR=...` builds one; `examples/modules/hello/` is a
  complete example. `mm41` is linked with `-rdynamic` so a module uses the
  calculator's own API.
- Binary `mm41`, config in `~/.config/mm41/`, prompt `MM-41>>`, man page
  `mm41.1`, manual `doc/mm41-manual.pdf` with a Modules chapter.

The MM-15 history below is kept for reference.

## MM-15 2.1

- **User manual.** `doc/manual.tex` builds `doc/mm41-manual.pdf` (`make
  manual`, LuaLaTeX or XeLaTeX); the command reference appendix is generated
  from the dispatch table by `mm41 --commands-tex`. The PDF is in the
  repository and is installed with the docs. README and MANUAL.md rewritten;
  the help screen (`help`) rewritten; every command's help text revised so
  that stack pictures and descriptions match what the command does.
- **Argument orders and semantics made consistent** (each one was documented
  one way and implemented another):
  - `set_aij` takes `A i j x` (matrix deepest, like `get_aij`) and consumes
    `i j x`; a complex `x` promotes a real matrix.
  - `rrange` takes `start step end`; 1.x ignored two of its arguments.
  - `days2eoy` takes the date on the stack; 1.x ignored it and used today.
  - `eq`/`neq` compare strings.
  - Column and row reductions (`csum`, `cmean`, `cvar`, `cmin`, `cmax` and the
    `r` variants) consume the matrix; 1.x left it underneath. The `cond`,
    `norm2`, `normfro` and `irr` macros no longer leave debris on the stack.
  - `A x ^` with a non-integer `x` is an error; 1.x silently truncated the
    exponent and returned the identity for `A 0.5 ^`.
  - `intg`/`frac` documented as HP truncation toward zero (unchanged).
  - `gravity` is 9.80665.
- **Real and complex matrices mix** in `+ - * .* ./ .^` and with complex
  scalars; the real operand is promoted.
- `print` prints a matrix in full; complex literals may contain spaces,
  `( 1 , 2 )`; `[2, 2, "file"]` may contain spaces and reports a missing file
  instead of pushing a null matrix.
- Programs: comment lines are skipped, the listing is printed only in verbose
  mode, and the never-settable `ctr_*?` tests are gone.
- Word definitions are not echoed in batch mode (unless `verbose_mode = 1`).
- `npv` macro simplified to `cashflows rate npv`. `make install` now
  replaces `~/.config/mm41/predefined_macros.txt` with the shipped file, so
  the macros always match the program; `config.txt` and `user_words.txt` are
  still left alone. Keep your own macros in `user_words.txt` (or in a file you
  `loadwords`), not in the macro file.
- `errors.c` builds with gcc 14's `-Wformat-overflow`.
- GSL 2.8 reports a singular matrix from the LU decomposition itself; `minv`
  now says "singular" and `det` returns 0 with any GSL version. Negative
  zero prints as 0.

## MM-15 2.0

Six commits on top of 1.1, each buildable and tested on its own.

### Behaviour you will notice

- **Atomic errors.** Every top-level token snapshots the stack; on error
  the stack is restored, the message goes to stderr, and the rest of the
  line is skipped. A user word is one token. Nothing silently pushes `0`,
  `inf` or a half-result any more; `fzero` no longer "pushes a back".
- **One promotion rule.** `sqrt`, `ln`, `log`, `asin`, `acos`, `acosh`,
  `atanh` and `^` promote a real argument outside the real domain to a
  complex result, and a real matrix with one such entry to a complex
  matrix. Previously `-1 sqrt` promoted but `-8 3 inv ^` gave `nan`.
  Division by zero is an error.
- **`sto` takes `value n sto`** (HP order; 1.x took `n value sto`). The
  `sto_add`/`sto_sub`/`sto_mul`/`sto_div` macros are updated.
- **`substr` takes `s start end substr`** (the string deepest, 0-based,
  end exclusive), matching its own help; 1.x wanted the string on top.
- **`roll` takes its depth from the stack** (`1 roll` = swap, `2 roll` =
  rot); 1.x ignored the argument and always rolled depth 2. `pick` and
  `depth` are new.
- **Numerical methods take `@name`** execution tokens instead of a word
  selected by index with `selword`: `0 pi @sin integrate`,
  `a b @f fzero`. `set_intg_tol`/`set_f0_tol` are replaced by one `tol`.
- **Batch mode.** `-e EXPR`, `-f FILE`, or stdin that is not a terminal:
  no splash, no per-line echo, final stack printed, exit status 1 on any
  error. `-n` skips the saved macros/words/config. `--commands` prints the
  command reference; `--version`.
- **Startup** no longer runs `curl` (1.1), reads config from
  `$XDG_CONFIG_HOME`/`$HOME` at run time (1.1), is silent on a missing
  config/macro file, and autoloads `user_words.txt`.
- **`roots` takes coefficients highest degree first**, like `pval` and
  Matlab. 1.x passed them to GSL in ascending order while `pval` used
  descending, so `[1 3 $ 1 -3 2] roots` gave 0.5 and 1 instead of 1 and 2.
  `pval` accepts a column vector and a complex x; both check their arguments.
- **Wrong argument types are errors, not crashes or leaks.** `join_v`,
  `join_h` and `pinv` segfaulted on a non-matrix; `det`, `eig`, `pval`,
  `roots`, `split_mat`, `cumsum_*`, `join_*`, `dow`, `dateplus`, `ddays`,
  `edmy` and the comparisons leaked their arguments on every call, and some
  thirty commands leaked the argument on a type error. A sweep of all
  built-ins over every combination of argument types, and a random fuzz, now
  run clean under ASan with leak detection.

### Words

- Compiled at definition time: unknown names are rejected immediately,
  control flow becomes branches. `if/else/then`, `begin/until`,
  `begin/while/repeat`, `do/loop/+loop` with `i`/`j` work inside words.
- Redefinition replaces the word; callers see the new body.
- Runaway recursion stops at 256 levels with an error (1.x crashed).
- Names are validated (no built-ins, no control words, no leading digit);
  tables grow dynamically; bodies are unbounded.
- Macros and words share one implementation; both files load in two passes
  (forward references work) and report the line of anything that fails.
- `\` and `#` start comments.

### Numerics (ported from MM-14 2.0, cross-checked against SciPy)

`integrate` (tanh-sinh with adaptive bisection; endpoint singularities at 0
exact), `romberg`, `fzero` (Brent), `solve` (one guess), `minimize`
(Brent), `deriv` (Ridders), `fsum`, `fprod`, `exec`, `tol`, `nevals`,
`interr`. 62 cases against SciPy: worst relative error 3.5e-11.

### Structure

- `src/ops.c` is the one dispatch table: name, minimum depth, function,
  category, stack picture, help, example. Dispatch, the depth check,
  `usage`, `listfcns`, tab completion and `COMMANDS.md` come from it. The
  250-line `strcmp` chain, `function_list.c` and `help.c`'s table are gone.
- `errors.c`: `mm_error()` replaces ~470 `fprintf(stderr)`/`perror` sites
  and counts errors; that count drives rollback and the exit status.
- `numerics.c` replaces `integration_and_zeros.c`.
- New built-ins: `conj` (was listed but not implemented), `pow` alias,
  `%`, `%t`, `%chg`, `mod`, `fact`, `ncr`, `npr`, `pick`, `depth`.
  Removed: the never-dispatched lowercase `goto/xeq/lbl/ctr_*/top_*?`
  names (programs use `LBL/GOTO/GOSUB/RTN/END` in files), `selword`,
  `set_intg_tol`, `set_f0_tol`.
- Tests: `tests/run_golden.sh` with `tests/golden/*.rpn` (full stdout +
  stderr + exit status) next to the existing `.t` cases; both run under
  ASan in `make test`. `make golden` regenerates the `.out` files.
- Repo: `README.html`, `README_MM-41.html`, `camel_2_snake.py`,
  `data/bbb`, LaTeX aux files, `.rpn_history` and editor files removed;
  `MANUAL.md` points at the generated `COMMANDS.md`; man page `mm41.1`;
  `make install` installs the man page and docs, `make uninstall` removes
  them.

## MM-15 1.1

- `5 sto` read past the stack (ASan abort); fixed.
- A self-calling word overflowed the C stack; nesting now stops at 256.
- Config paths resolved at run time instead of `-DHOME_DIR` at compile time.
- Startup no longer runs `curl` against ipify/ip-api/wttr.in.
