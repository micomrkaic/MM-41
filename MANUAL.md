# MM-41 quick reference

The full manual is [doc/mm41-manual.pdf](doc/mm41-manual.pdf) (also installed
under `share/doc/mm41`). This page is the one-screen version; the complete
command list with stack pictures is [COMMANDS.md](COMMANDS.md).

## Input

| | |
|---|---|
| numbers | `3` `-1.25` `.5` `1e3`; `0.1d` decimal (34 digits), `2b` binary; `decimal`/`binary` set the mode |
| complex | `(1,2)` `(-1.2e-4, 0.7e2)` |
| strings | `"text"` with `\"` `\\` `\n` `\t` |
| matrices | `[rows cols $ a11 a12 ...]` row-major; `[2, 2, "file.txt"]` from a file |
| comments | `\` or `#` to end of line |
| execution token | `@name` — a reference to a command or word, for the numerical methods |
| shell | `!command` pushes the command's output as a string |

All input is case sensitive. The stack is printed after every line:
`[0]` is the bottom, the highest index is the top. `undo` restores the stack
before the previous line; `q` quits.

## Stack

`dup drop swap over nip tuck` · `n pick` (copy depth n) · `n roll` (move depth
n to top; `1 roll` = swap, `2 roll` = rot) · `depth` · `clst` · `print ps pm` ·
`setprec` · `sfs` (fixed ↔ scientific) · `savestack loadstack`

## Arithmetic and functions

`+ - * / ^` on reals, complex and matrices (`*` is the matrix product, `A B /`
is `A B⁻¹`, `A n ^` an integer matrix power); `.* ./ .^` elementwise.
`chs inv abs sqrt intg frac mod fact ncr npr` · `% %t %chg` (HP, keep y) ·
`exp ln log` · `sin cos tan asin acos atan sinh cosh tanh asinh acosh atanh` ·
`re im arg conj re2c split_c j2r` · `npdf ncdf nquant gamma ln_gamma beta ln_beta` ·
`pi e gravity inf nan` · `eq neq lt leq gt geq and or not` (flags 0/1).

Promotion: `-1 sqrt` → `(0,1)`, `2 asin`, `0.5 acosh`, `2 atanh`, `-1 ln`,
`-8 3 inv ^` all return complex. Division by zero is an error. Unary
functions apply elementwise to matrices.

## Decimals

Decimal absorbs binary: `0.1d 0.2 +` is exactly `0.3`. Arithmetic, `% %t
%chg mod`, `sqrt exp ln log intg frac`, comparisons, registers and control
words take decimals; trig, complex, matrices and numerics want `>bin` first
(a decimal used as an index, count, dimension, precision or flag is
converted). `>dec` · `>bin` · `x n round` (half-even) · `isdec` · `decmode`.
Fixed display rounds to `setprec` places; `sfs` shows a decimal as stored.

## Finance (all decimal; rates in percent per period; cash received +)

TVM: `n i% pv pmt fv` store · `?n ?i% ?pv ?pmt ?fv` solve · `beg end` ·
`clrtvm tvm` · `amort` (prints) · `amortm` (n×4 matrix).
`cf rate npv` · `cf irr` — `cf` a vector in time order or N×2 amount/count.
`nom m nom>eff` · `eff m eff>nom` · `cost salvage life sl` · `… year soyd` ·
`… year factor db` · `cpn yld n redeem bprice` · `price cpn n redeem byield`.
Dated (dates `"d.m.yyyy"` or `YYYYMMDD`; `date>ymd ymd>date`): `M rate xnpv`,
`M xirr` (M = N×2 amount, date) · `settle maturity cpn yld redeem freq basis
xprice` · `… cpn price redeem freq basis xyield` · `settle maturity cpn freq
basis xaccr` · `d1 d2 basis yearfrac` (0 30/360, 1 act/act, 2 act/360,
3 act/365, 4 30E/360).

```
clrtvm 12 n 1 i% 10000 pv 0 fv ?pmt          \ -888.49
[6 1 $ -1000 300 300 300 300 300] 10 npv      \ 137.24
20080215 20171115 5.75 6.5 100 2 0 xprice     \ 94.6344
```

## Matrices

`eye ones zeroes rand randn` · `start step end rrange` · `tran '` · `minv pinv
det eig chol svd kron` · `dim` · `A i j get_aij` · `A i j x set_aij` (0-based) ·
`reshape split_mat diag to_diag join_v join_h` · `csum rsum cmean rmean cvar
rvar cmin rmin cmax rmax` (c = per column → row vector; r = per row → column
vector) · `cumsum_r cumsum_c` · `roots pval` (coefficients highest degree first).

## Strings, registers, dates

`scon` · `s start end substr` · `s2u s2l slen srev int2str` · `"1 2 +" eval`.
`x n sto` · `n rcl` · `pr ffr saveregs loadregs clregs` (64 registers, any type).
Dates are `"d.m.yyyy"`: `today now dateplus ddays dow edmy num2date days2eoy
tz_offset` · `date lat lon zone sunrise|sunset|dawn|dusk` (zone: IANA name or
UTC offset). Workdays (dates as `"d.m.yyyy"` or `YYYYMMDD`): `d1 d2 workdays`
(inclusive, NETWORKDAYS) · `date n workdayplus` (WORKDAY) · `date isworkday` ·
calendars `"us" usehol` / `"si" usehol` (built-in rules, saved in config) or
`"name" usehol` from `~/.config/mm41/holidays/name.txt` · `"us" holidays`,
`2027 "si" yholidays` (vectors) · `H sethol` · `hol` · `clrhol`.

## Words

```
: sq dup * ;                      one line; nothing after the ;
: fac dup 1 gt if dup 1 - fac * then ;
: sum 0 swap 1 + 1 do i + loop ;
```
Control words inside definitions: `flag if ... else ... then` ·
`begin ... flag until` · `begin ... flag while ... repeat` ·
`limit start do ... loop` / `n +loop` with `i`, `j`.
`listwords` · `help name` / `"name" usage` · `"name" delword` · `clrwords` · `savewords` /
`loadwords` (`user_words.txt`, loaded at startup) · `listmacros`.

## Modules

`modules` lists what is loaded. Word modules: `~/.config/mm41/modules/*.mm`
(loaded at startup), `"name" use`, `"name" unuse`; a file of `name body` or
`: name body ;` lines with an optional `\ module name version summary` header.
Compiled modules: `"file.so" load`, or drop the `.so` into the same directory;
`make shared-module DIR=...` builds one from `examples/modules/hello`-style
source. `make MODULES="..."` chooses the built-in modules.

## Numerical methods

`a b @f integrate` · `a b @f romberg` · `a b @f fzero` · `g @f solve` ·
`a b @f minimize` · `x @f deriv` · `lo hi @f fsum` / `fprod` · `@f exec` ·
`x tol` · `nevals` · `interr`. The function takes x and must leave one real.

## Programs and batch

`"file.prg" run` — one instruction per line: `LBL name`, `GOTO name`,
`GOSUB name`, `RTN`, `END`, and tests (`top_gt?`, `top_eq0?`, ...) that skip
the next line when false. `"file" batch` runs ordinary lines.

`mm41 -e EXPR` · `-f FILE` · stdin · `-n` (no saved state) · `--modules` ·
`--commands` · `--version`. Exit status 1 if any error occurred.

## Files

`$XDG_CONFIG_HOME/mm41/` (default `~/.config/mm41/`): `config.txt` (precision, display, number mode, holiday calendar, tolerance),
`modules/*.mm`, `modules/*.so`, `holidays/*.txt`, `user_words.txt`, `registers.txt`,
`stack_backup.dat`, `.rpn_history`.
