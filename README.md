# MM-41 — a modular RPN calculator

[![ci](https://github.com/micomrkaic/MM-41/actions/workflows/ci.yml/badge.svg)](https://github.com/micomrkaic/MM-41/actions/workflows/ci.yml)

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
core         2.2         91  arithmetic, functions, logic, the stack, words and the session
decimal      1.0          7  decimal numbers for money: number mode, conversions, rounding
finance      1.1         35  time value of money (level or growing payments), cash flows, depreciation, bonds, day counts
complex      1.0          7  complex numbers: parts, argument, conjugate, construction
special      1.1         24  gamma, beta, the normal distribution, error, Bessel, Airy, elliptic, zeta and more
stat         1.0         27  descriptive statistics, curve fitting, regression, t/chi-square/F/binomial/Poisson distributions
random       1.0         16  draws from named distributions, resampling and Monte Carlo over a word
ode          1.0          2  initial-value problems y' = f(t, y) over a word, scalar or system (RKF45)
survival     1.1         31  lifetime laws (Gompertz-Makeham and others), survival and hazard, life tables, annuities, Kaplan-Meier, fits
income       1.1         38  income distributions (Dagum, Singh-Maddala, Pareto, lognormal and more), inequality and poverty measures, fits
plot         1.0         25  graphs in the terminal: lines, points, steps, bars, histograms, functions; log axes, legend, SVG export
matrices     1.1         43  linear algebra, matrix construction and reduction, polynomials, reading tables from files
numerics     1.0         12  integration, roots, minima, derivatives and sums over any word
strings      1.0          7  string construction and manipulation
registers    1.0          7  64 storage registers of any type
dates        1.1         22  calendar arithmetic, workdays and holidays, time zones, sunrise and sunset
programs     1.0          1  HP-41 style programs with labels, jumps and subroutines
std          1.0         28  standard word module: powers, norms, storage arithmetic, locations
```

The **[user manual](doc/mm41-manual.pdf)** (PDF) covers everything
with a complete command reference. `man mm41` and [COMMANDS.md](COMMANDS.md)
are the short forms; inside the calculator, `help` for the overview, `help npv`
for one command in full (what it takes, what it leaves, its example run),
`help modules` for what modules are and how to add one (also `help words`,
`help finance`, …), `help holiday` to search, `listfcns` and `modules`;
`manual` opens the user manual in the pager, `manual survival` at that
chapter, `manual npv` at the first mention (`n` next, `q` back).
`skin hp41`, `skin hp12c` or `skin hp48` draws the stack as that calculator's
display would have (twelve-character LCD with annunciators, seven-segment,
the 48's screen; `skin hp48px` the 48 as a dot matrix); `skin plain` is
MM-41's own listing.
From the shell: `mm41 -e "2 10 ^"`, `mm41 -p -e "[1 2; 3 4] det"` (the top,
plainly), `echo "2 3 + ." | mm41 -q -`, or a script with `#!/usr/bin/env mm41`
on its first line that takes arguments (`./loan 300000 6 30`) and prints with
`.`; `-x` stops at the first error, the exit status is 0/1/2.
Commands that take a name take it as a string (`"const" use`, `"us" usehol`);
at the prompt a line of just the command and a bare name does the same thing
(`use const`, `usehol us`, `load hello.so`), and TAB completes the name —
after `help `, modules, topics and categories too.

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
  fv g%` (growth of the payment per period) with `?n ?i% ?pv ?pmt ?fv ?g%`,
  `beg`/`end`, `amort`/`amortm`; `npv`/`irr`
  over cash-flow vectors; `nom>eff`, `eff>nom`; `sl`, `soyd`, `db`; `bprice`,
  `byield`; dated `xnpv`, `xirr`, `xprice`, `xyield`, `xaccr`, `yearfrac` with
  Excel's five day-count bases.
- **Matrices** via GSL: products, elementwise operators, inverse, pseudoinverse,
  determinant, eigen-, Cholesky and singular value decompositions, Kronecker
  product, reshaping and joining, column/row statistics, polynomial roots and
  evaluation. Real and complex matrices mix.
- **Words**, compiled at definition time, with `if/else/then`, `begin/until`,
  `begin/while/repeat`, `do/loop/+loop` and `i`/`j`; recursion allowed.
- **Statistics and simulation**: `mean sd median quantile corr`, the Advantage
  Pac's `linfit expfit logfit powfit`, multiple regression `ols`/`olstab` with
  standard errors, t/chi-square/F/binomial/Poisson distributions; random
  draws, bootstrap `resample`, and `n @word sims` Monte Carlo with `mcstats`.
- **Differential equations**: `y0 t0 t1 n @f odeint` (RKF45), scalar or system.
- **Survival analysis**: select a lifetime law once (`"makeham" [a b c] setlaw`;
  exponential, Weibull, Gompertz, Gompertz-Makeham, Siler, log-logistic,
  lognormal, gamma), then `t surv haz cumhaz`, `t elife`, `lifemean lifemed
  lifemode`, `x n tpx`, `x0 x1 lifetab`, the actuarial `ax axn Ax Axn`;
  Kaplan-Meier `km`, `nelsonaalen`, `logrank`, and `lifefit` by maximum
  likelihood with right censoring; `lawfit`/`lawfitl` fit a law to a
  published life table (ages with qx or lx).
- **Income distribution**: select a family once (`"dagum" [a b p] setdist`;
  Pareto, lognormal, Dagum, Singh-Maddala, Fisk, gamma, Weibull), then
  `incmean incmed incq`, `gini theil mld atkinson`, `lorenz top deciles
  palma qratio`, `headcount povgap fgt`, `incfit`; and the same from data:
  `ginid theild atkinsond lorenzd decilesd palmad headcountd povgapd fgtd`;
  `incfits` fits a family to published quintile or decile shares, `incfitl`
  to Lorenz-curve points.
- **Graphs**: `x y plot`, `scatter`, `steps`, `bars`, `v k hplot`, `M mplot`,
  `a b @f fplot` draw in the terminal (two pixels a cell, round-number ticks,
  a legend, colour); `hold` overlays, `pxlog pylog pgrid pxrange ptitle
  plegend` decorate, `"fig.svg" psvg` writes the figure as vector graphics.
- **Files**: `"table.txt" readmat` reads a table as it is (any separators,
  comments and a header skipped), `readvec` every number as a column,
  `"t.txt" 2 readcol` one column; `getcol`/`getrow` take one out of a
  matrix.
- **Special functions**: `erf`, incomplete gamma and beta, Bessel, Airy,
  elliptic, zeta, digamma, Lambert W; the `const` word module has the physical
  constants (`c h G kB qe NA ...`).
- **Numerical methods** over any word or command: `integrate` (tanh-sinh),
  `romberg`, `fzero`, `solve`, `minimize`, `deriv`, `fsum`, `fprod`; a
  function with a parameter is a word that fixes it (`: j2 2 besselj ;` then
  `0 10 @j2 integrate`), and `help` shows such an example for each of them.
- **Strings, registers, dates and the sun**: 64 registers of any type; date
  arithmetic; workdays with holiday calendars (`workdays`, `workdayplus`,
  Excel's NETWORKDAYS/WORKDAY conventions; built-in US federal and Slovenian
  rules, or your own file); sunrise, sunset and civil dawn/dusk with IANA
  time zones.
- **HP-41 style programs** from files (`LBL`, `GOTO`, `GOSUB`, `RTN`, tests).
- **Batch mode**: `-e EXPR`, `-f FILE`, or piped stdin; exit status 1 on any
  error.

## Learning it

`tutor` at the prompt opens a fifteen-chapter tour you type along with, in
the manner of vimtutor: a passage, a line to type, and the tutor moves on
when your stack shows what the lesson expects. `examples 7` (or `examples
finance`) prints a chapter as a cookbook with every result; `help <name>`
describes any command; `manual <chapter>` opens the manual in the pager.

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
"us" usehol "1.1.2026" "31.12.2026" workdays   \ 250  (261 weekdays, 11 federal holidays)
"21.6.2026" 46.24 14.36 "Europe/Ljubljana" sunset     \ "20:58"
```

```sh
mm41 -e "2 10 ^"                         # batch: prints the final stack, exits 1 on error
echo "[2 2 $ 1 2 3 4] det" | mm41 -n     # -n: ignore saved words, modules and config
```

## Input in one paragraph

Tokens are separated by whitespace and all input is case sensitive. Numbers:
`3`, `-1.2e-4`, `.5`; `0.1d` is a decimal, `2b` a binary double. Complex: `(1,2)`. Strings: `"text"` with `\" \\ \n \t`.
Matrices: `[1 2 3]` (a row vector), `[1 2; 3 4]` (a semicolon ends a row),
`[rows cols $ elements...]`; `"file" readmat` or `readvec` to read one from a
file, or `[rows, cols, "file"]` giving the shape. `\` and `#` start comments. `@name` pushes an execution token for
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
