#define _POSIX_C_SOURCE 200809L
/*
 * This file is part of MM-41
 *
 * MM-41 is free software: 
 * you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MM-41 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MM-41. If not, see <https://www.gnu.org/licenses/>.
 */

#include "errors.h"
#include <gsl/gsl_math.h>
#include <math.h>
#include <readline/history.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ops.h"
#include "module.h"
#include "binary_fun.h"
#include "compare_fun.h"
#include "globals.h"
#include "help.h"
#include "math_helpers.h"
#include "print_fun.h"
#include "stack_io.h"
#include "unary_fun.h"
#include "words.h"
#include "eval_fun.h"

/* ---- argument helpers shared with the modules ---- */
int pop_string_arg(Stack *stack, const char *what, char **out) {
  stack_element t = pop(stack);
  if (t.type != TYPE_STRING) {
    stack_element_free(&t);
    return mm_error("%s: expected a string on the stack", what);
  }
  *out = t.string;
  return 0;
}

int pop_real_arg(Stack *stack, const char *what, double *out) {
  if (stack->items[stack->top].type != TYPE_REAL) return mm_error("%s: expected a real number", what);
  *out = pop(stack).real;
  return 0;
}

int pop_two_reals(Stack *stack, const char *what, double *y, double *x) {
  if (stack->items[stack->top].type != TYPE_REAL || stack->items[stack->top - 1].type != TYPE_REAL)
    return mm_error("%s: needs two real numbers", what);
  *x = pop(stack).real;
  *y = pop(stack).real;
  return 0;
}

static int op_eval(Stack *stack) {
  char *s;
  if (pop_string_arg(stack, "eval", &s) != 0) return 0;
  evaluate_line(stack, s);
  free(s);
  return 0;
}

static int op_batch(Stack *stack) {
  char *s;
  if (pop_string_arg(stack, "batch", &s) != 0) return 0;
  run_batch(stack, s);
  free(s);
  return 0;
}

static int op_modules(Stack *stack) { (void)stack; list_modules(); skip_stack_printing = true; return 0; }
static int op_pm(Stack *stack) { print_matrix(stack); skip_stack_printing = true; return 0; }
static int op_use(Stack *stack) {
  char *s; if (pop_string_arg(stack, "use", &s) != 0) return 0;
  use_word_module(s); free(s); return 0;
}
static int op_load(Stack *stack) {
  char *s; if (pop_string_arg(stack, "load", &s) != 0) return 0;
  load_shared_module(s); free(s); return 0;
}
static int op_unuse(Stack *stack) {
  char *s; if (pop_string_arg(stack, "unuse", &s) != 0) return 0;
  unuse_word_module(s); free(s); return 0;
}

/* ---- small operations that live here ---- */

/* n roll: move the item at depth n (0 = top) to the top; 1 roll = swap. */
static int op_roll_n(Stack *stack) {
  stack_element e = pop(stack);
  if (e.type != TYPE_REAL || e.real != (int)e.real || e.real < 0) {
    stack_element_free(&e);
    return mm_error("roll: depth must be a non-negative integer");
  }
  stack_roll(stack, (int)e.real);
  return 0;
}

/* n pick: copy the item at depth n (0 = top) to the top; 0 pick = dup. */
static int op_pick(Stack *stack) {
  stack_element e = pop(stack);
  if (e.type != TYPE_REAL || e.real != (int)e.real || e.real < 0) {
    stack_element_free(&e);
    return mm_error("pick: depth must be a non-negative integer");
  }
  int n = (int)e.real;
  if (stack->top - n < 0) return mm_error("pick: depth %d exceeds the stack", n);
  if (stack->top + 1 >= STACK_SIZE) return mm_error("Stack overflow");
  if (stack_element_clone(&stack->items[stack->top + 1], &stack->items[stack->top - n]) != 0)
    return mm_error("pick: failed to copy element");
  stack->top++;
  return 0;
}

static int op_depth(Stack *stack) { push_real(stack, stack->top + 1); return 0; }

static int op_pct(Stack *stack) {          /* y x -- y y*x/100  (HP %) */
  double y, x;
  if (pop_two_reals(stack, "%", &y, &x)) return 0;
  push_real(stack, y); push_real(stack, y * x / 100.0); return 0;
}
static int op_pctt(Stack *stack) {         /* y x -- y 100*x/y  (HP %T) */
  double y, x;
  if (pop_two_reals(stack, "%t", &y, &x)) return 0;
  if (y == 0.0) return mm_error("%%t: total is zero");
  push_real(stack, y); push_real(stack, 100.0 * x / y); return 0;
}
static int op_pctchg(Stack *stack) {       /* y x -- y 100*(x-y)/y  (HP delta%) */
  double y, x;
  if (pop_two_reals(stack, "%chg", &y, &x)) return 0;
  if (y == 0.0) return mm_error("%%chg: base is zero");
  push_real(stack, y); push_real(stack, 100.0 * (x - y) / y); return 0;
}
static int op_mod(Stack *stack) {          /* floored modulo, sign of x */
  double y, x;
  if (pop_two_reals(stack, "mod", &y, &x)) return 0;
  if (x == 0.0) return mm_error("mod: modulus is zero");
  double r = fmod(y, x);
  if (r != 0.0 && ((r < 0) != (x < 0))) r += x;
  push_real(stack, r); return 0;
}
static int op_fact(Stack *stack) {
  if (stack->items[stack->top].type != TYPE_REAL) return mm_error("fact: needs a real number");
  double x = pop(stack).real;
  if (x < 0.0 && x == floor(x)) return mm_error("fact: negative integer");
  double r = tgamma(x + 1.0);
  if (!isfinite(r)) return mm_error("fact: overflow");
  push_real(stack, x == floor(x) && x <= 22 ? round(r) : r); return 0;
}
static int op_ncr(Stack *stack) {
  double n, k;
  if (pop_two_reals(stack, "ncr", &n, &k)) return 0;
  if (n != floor(n) || k != floor(k) || n < 0 || k < 0 || k > n) return mm_error("ncr: need integers 0 <= k <= n");
  if (k > n - k) k = n - k;
  double r = 1.0;
  for (double i = 1.0; i <= k; i += 1.0) r = r * (n - k + i) / i;
  push_real(stack, r < 9007199254740992.0 ? round(r) : r); return 0;
}
static int op_npr(Stack *stack) {
  double n, k;
  if (pop_two_reals(stack, "npr", &n, &k)) return 0;
  if (n != floor(n) || k != floor(k) || n < 0 || k < 0 || k > n) return mm_error("npr: need integers 0 <= k <= n");
  double r = 1.0;
  for (double i = n - k + 1.0; i <= n; i += 1.0) r *= i;
  if (!isfinite(r)) return mm_error("npr: overflow");
  push_real(stack, r); return 0;
}

static int op_add(Stack *stack) { (void)stack; (void)(add_top_two(stack)); return 0; }
static int op_sub(Stack *stack) { (void)stack; (void)(sub_top_two(stack)); return 0; }
static int op_mul(Stack *stack) { (void)stack; (void)(mul_top_two(stack)); return 0; }
static int op_div(Stack *stack) { (void)stack; (void)(div_top_two(stack)); return 0; }
static int op_pow_(Stack *stack) { (void)stack; (void)(pow_top_two(stack)); return 0; }
static int op_pow(Stack *stack) { (void)stack; (void)(pow_top_two(stack)); return 0; }
static int op_dot_mul(Stack *stack) { (void)stack; (void)(dot_mult_top_two(stack)); return 0; }
static int op_dot_div(Stack *stack) { (void)stack; (void)(dot_div_top_two(stack)); return 0; }
static int op_dot_pow(Stack *stack) { (void)stack; (void)(dot_pow_top_two(stack)); return 0; }
static int op_chs(Stack *stack) { (void)stack; (void)(chs_wrapper(stack)); return 0; }
static int op_inv(Stack *stack) { (void)stack; (void)(inv_wrapper(stack)); return 0; }
static int op_abs(Stack *stack) { (void)stack; (void)(abs_wrapper(stack)); return 0; }
static int op_sqrt(Stack *stack) { (void)stack; (void)(sqrt_wrapper(stack)); return 0; }
static int op_frac(Stack *stack) { (void)stack; (void)(frac_wrapper(stack)); return 0; }
static int op_intg(Stack *stack) { (void)stack; (void)(intg_wrapper(stack)); return 0; }
static int op_exp(Stack *stack) { (void)stack; (void)(exp_wrapper(stack)); return 0; }
static int op_ln(Stack *stack) { (void)stack; (void)(ln_wrapper(stack)); return 0; }
static int op_log(Stack *stack) { (void)stack; (void)(log_wrapper(stack)); return 0; }
static int op_sin(Stack *stack) { (void)stack; (void)(sin_wrapper(stack)); return 0; }
static int op_cos(Stack *stack) { (void)stack; (void)(cos_wrapper(stack)); return 0; }
static int op_tan(Stack *stack) { (void)stack; (void)(tan_wrapper(stack)); return 0; }
static int op_asin(Stack *stack) { (void)stack; (void)(asin_wrapper(stack)); return 0; }
static int op_acos(Stack *stack) { (void)stack; (void)(acos_wrapper(stack)); return 0; }
static int op_atan(Stack *stack) { (void)stack; (void)(atan_wrapper(stack)); return 0; }
static int op_sinh(Stack *stack) { (void)stack; (void)(sinh_wrapper(stack)); return 0; }
static int op_cosh(Stack *stack) { (void)stack; (void)(cosh_wrapper(stack)); return 0; }
static int op_tanh(Stack *stack) { (void)stack; (void)(tanh_wrapper(stack)); return 0; }
static int op_asinh(Stack *stack) { (void)stack; (void)(asinh_wrapper(stack)); return 0; }
static int op_acosh(Stack *stack) { (void)stack; (void)(acosh_wrapper(stack)); return 0; }
static int op_atanh(Stack *stack) { (void)stack; (void)(atanh_wrapper(stack)); return 0; }
static int op_pi(Stack *stack) { (void)stack; (void)(push_real(stack, M_PI)); return 0; }
static int op_e(Stack *stack) { (void)stack; (void)(push_real(stack, exp(1.0))); return 0; }
static int op_gravity(Stack *stack) { (void)stack; (void)(push_real(stack, 9.80665)); return 0; }
static int op_inf(Stack *stack) { (void)stack; (void)(push_real(stack, INFINITY)); return 0; }
static int op_nan(Stack *stack) { (void)stack; (void)(push_real(stack, NAN)); return 0; }
static int op_eq(Stack *stack) { (void)stack; (void)(dot_cmp_top_two(stack, CMP_EQ)); return 0; }
static int op_neq(Stack *stack) { (void)stack; (void)(dot_cmp_top_two(stack, CMP_NE)); return 0; }
static int op_lt(Stack *stack) { (void)stack; (void)(dot_cmp_top_two(stack, CMP_LT)); return 0; }
static int op_leq(Stack *stack) { (void)stack; (void)(dot_cmp_top_two(stack, CMP_LE)); return 0; }
static int op_gt(Stack *stack) { (void)stack; (void)(dot_cmp_top_two(stack, CMP_GT)); return 0; }
static int op_geq(Stack *stack) { (void)stack; (void)(dot_cmp_top_two(stack, CMP_GE)); return 0; }
static int op_and(Stack *stack) { (void)stack; (void)(dot_cmp_top_two(stack, CMP_AND)); return 0; }
static int op_or(Stack *stack) { (void)stack; (void)(dot_cmp_top_two(stack, CMP_OR)); return 0; }
static int op_not(Stack *stack) { (void)stack; (void)(logical_not_wrapper(stack)); return 0; }
static int op_dup(Stack *stack) { (void)stack; (void)(stack_dup(stack)); return 0; }
static int op_drop(Stack *stack) { (void)stack; (void)(pop_and_free(stack)); return 0; }
static int op_swap(Stack *stack) { (void)stack; (void)(swap(stack)); return 0; }
static int op_over(Stack *stack) { (void)stack; (void)(stack_over(stack)); return 0; }
static int op_nip(Stack *stack) { (void)stack; (void)(stack_nip(stack)); return 0; }
static int op_tuck(Stack *stack) { (void)stack; (void)(stack_tuck(stack)); return 0; }
static int op_clst(Stack *stack) { (void)stack; (void)(free_stack(stack)); return 0; }
static int op_savestack(Stack *stack) { (void)stack; (void)(save_stack_to_file(stack, STACK_PATH)); return 0; }
static int op_loadstack(Stack *stack) { (void)stack; (void)(load_stack_from_file(stack, STACK_PATH)); return 0; }
static int op_print(Stack *stack) { (void)stack; (void)(print_top_scalar(stack)); return 0; }
static int op_ps(Stack *stack) { (void)stack; (void)(print_stack(stack, NULL)); return 0; }
static int op_setprec(Stack *stack) { (void)stack; (void)(set_print_precision(stack)); return 0; }
static int op_sfs(Stack *stack) { (void)stack; (void)(swap_fixed_scientific()); return 0; }
static int op_listwords(Stack *stack) { (void)stack; (void)(list_words()); return 0; }
static int op_listmacros(Stack *stack) { (void)stack; (void)(list_macros()); return 0; }
static int op_loadwords(Stack *stack) { (void)stack; (void)(load_words_from_file()); return 0; }
static int op_savewords(Stack *stack) { (void)stack; (void)(save_words_to_file()); return 0; }
static int op_delword(Stack *stack) { (void)stack; (void)(delete_word(stack)); return 0; }
static int op_clrwords(Stack *stack) { (void)stack; (void)(clear_words()); return 0; }
static int op_help(Stack *stack) { (void)stack; (void)(help_menu()); return 0; }
static int op_listfcns(Stack *stack) { (void)stack; (void)(list_all_functions_sorted()); return 0; }
static int op_clrhist(Stack *stack) { (void)stack; (void)(clear_history()); return 0; }
static int op_fuck(Stack *stack) { (void)stack; (void)(whose_place()); return 0; }

static const Op core_table[] = {
  /* ---- Arithmetic ---- */
  { "+", 2, op_add, "Arithmetic", "y x -- y+x", "add scalars or matrices; a scalar added to a matrix is added elementwise", "3 4 +" },
  { "-", 2, op_sub, "Arithmetic", "y x -- y-x", "subtract scalars or matrices (a scalar elementwise)", "10 4 -" },
  { "*", 2, op_mul, "Arithmetic", "y x -- y*x", "multiply scalars, or matrix product for matrices", "[2 2 $ 1 2 3 4] [2 1 $ 1 1] *" },
  { "/", 2, op_div, "Arithmetic", "y x -- y/x", "divide scalars, or a matrix by a scalar; division by zero is an error", "1 3 /" },
  { "^", 2, op_pow_, "Arithmetic", "y x -- y^x", "power; a square matrix to an integer power is a matrix power; a negative base with a fractional exponent promotes to complex", "2 10 ^" },
  { "pow", 2, op_pow, "Arithmetic", "y x -- y^x", "power (alias of ^)", "2 10 pow" },
  { ".*", 2, op_dot_mul, "Arithmetic", "A B -- A.*B", "elementwise matrix product", "[2 2 $ 1 2 3 4] [2 2 $ 1 1 2 2] .*" },
  { "./", 2, op_dot_div, "Arithmetic", "A B -- A./B", "elementwise division (matrix or scalar operands)", "[2 2 $ 1 2 3 4] [2 2 $ 1 2 1 2] ./" },
  { ".^", 2, op_dot_pow, "Arithmetic", "A B -- A.^B", "elementwise power (matrix or scalar operands)", "[2 2 $ 1 2 3 4] 2 .^" },
  { "chs", 1, op_chs, "Arithmetic", "x -- -x", "change sign (elementwise for matrices)", "5 chs" },
  { "inv", 1, op_inv, "Arithmetic", "x -- 1/x", "reciprocal (elementwise for matrices; see minv for the matrix inverse)", "4 inv" },
  { "abs", 1, op_abs, "Arithmetic", "z -- |z|", "absolute value; magnitude for complex; elementwise for matrices", "(3,4) abs" },
  { "sqrt", 1, op_sqrt, "Arithmetic", "x -- sqrt(x)", "square root; a negative real promotes to complex", "-4 sqrt" },
  { "%", 2, op_pct, "Arithmetic", "y x -- y y*x/100", "percent of y (HP %; keeps y)", "200 25 %" },
  { "%t", 2, op_pctt, "Arithmetic", "y x -- y 100*x/y", "x as a percent of the total y (HP %T; keeps y)", "200 50 %t" },
  { "%chg", 2, op_pctchg, "Arithmetic", "y x -- y 100*(x-y)/y", "percent change from y to x (HP delta%; keeps y)", "80 100 %chg" },
  { "mod", 2, op_mod, "Arithmetic", "y x -- y mod x", "floored modulo (result has the sign of x)", "-7 3 mod" },
  { "fact", 1, op_fact, "Arithmetic", "x -- x!", "factorial; Gamma(x+1) for non-integers", "5 fact" },
  { "ncr", 2, op_ncr, "Arithmetic", "n k -- C(n,k)", "combinations", "52 5 ncr" },
  { "npr", 2, op_npr, "Arithmetic", "n k -- P(n,k)", "permutations", "10 3 npr" },
  { "frac", 1, op_frac, "Arithmetic", "x -- frac(x)", "fractional part, x - intg(x), with the sign of x", "3.7 frac" },
  { "intg", 1, op_intg, "Arithmetic", "x -- int(x)", "integer part, truncated toward zero as on HP calculators (-3.7 gives -3)", "3.7 intg" },
  /* ---- Exp/Log ---- */
  { "exp", 1, op_exp, "Exp/Log", "x -- e^x", "exponential (elementwise for matrices)", "1 exp" },
  { "ln", 1, op_ln, "Exp/Log", "x -- ln(x)", "natural logarithm; a negative real promotes to complex", "10 ln" },
  { "log", 1, op_log, "Exp/Log", "x -- log10(x)", "base-10 logarithm; a negative real promotes to complex", "100 log" },
  /* ---- Trigonometry ---- */
  { "sin", 1, op_sin, "Trigonometry", "z -- sin(z)", "sine of a real or complex argument (radians); elementwise for matrices", "0.5 sin" },
  { "cos", 1, op_cos, "Trigonometry", "z -- cos(z)", "cosine of real or complex argument", "1 cos" },
  { "tan", 1, op_tan, "Trigonometry", "z -- tan(z)", "tangent of real or complex argument", "0.3 tan" },
  { "asin", 1, op_asin, "Trigonometry", "z -- asin(z)", "inverse sine (principal value)", "0.5 asin" },
  { "acos", 1, op_acos, "Trigonometry", "z -- acos(z)", "inverse cosine (principal value)", "0.5 acos" },
  { "atan", 1, op_atan, "Trigonometry", "z -- atan(z)", "inverse tangent (principal value)", "1 atan" },
  { "sinh", 1, op_sinh, "Trigonometry", "z -- sinh(z)", "hyperbolic sine", "1 sinh" },
  { "cosh", 1, op_cosh, "Trigonometry", "z -- cosh(z)", "hyperbolic cosine", "1 cosh" },
  { "tanh", 1, op_tanh, "Trigonometry", "z -- tanh(z)", "hyperbolic tangent", "1 tanh" },
  { "asinh", 1, op_asinh, "Trigonometry", "z -- asinh(z)", "inverse hyperbolic sine", "1 asinh" },
  { "acosh", 1, op_acosh, "Trigonometry", "z -- acosh(z)", "inverse hyperbolic cosine", "2 acosh" },
  { "atanh", 1, op_atanh, "Trigonometry", "z -- atanh(z)", "inverse hyperbolic tangent", "0.5 atanh" },
  /* ---- Constants ---- */
  { "pi", 0, op_pi, "Constants", "-- π", "push π", "pi" },
  { "e", 0, op_e, "Constants", "-- e", "push Euler's number", "e" },
  { "gravity", 0, op_gravity, "Constants", "-- g", "standard gravity, 9.80665 m/s^2", "gravity" },
  { "inf", 0, op_inf, "Constants", "-- +∞", "positive infinity", "inf" },
  { "nan", 0, op_nan, "Constants", "-- NaN", "not-a-number", "nan" },
  /* ---- Logic ---- */
  { "eq", 2, op_eq, "Logic", "a b -- flag", "1 if a equals b, else 0; numbers, strings, or elementwise for matrices", "2 2 eq" },
  { "neq", 2, op_neq, "Logic", "a b -- flag", "1 if a differs from b; numbers, strings, or elementwise for matrices", "2 3 neq" },
  { "lt", 2, op_lt, "Logic", "a b -- flag", "1 if a < b (complex numbers compare by magnitude); elementwise for matrices", "2 3 lt" },
  { "leq", 2, op_leq, "Logic", "a b -- flag", "1 if a <= b; elementwise for matrices", "2 3 leq" },
  { "gt", 2, op_gt, "Logic", "a b -- flag", "1 if a > b; elementwise for matrices", "3 2 gt" },
  { "geq", 2, op_geq, "Logic", "a b -- flag", "1 if a >= b; elementwise for matrices", "3 2 geq" },
  { "and", 2, op_and, "Logic", "a b -- flag", "logical and (0 is false, anything else true)", "1 0 and" },
  { "or", 2, op_or, "Logic", "a b -- flag", "logical or", "1 0 or" },
  { "not", 1, op_not, "Logic", "a -- flag", "logical not", "0 not" },
  /* ---- Stack ---- */
  { "dup", 1, op_dup, "Stack", "x -- x x", "duplicate top of stack", "5 dup" },
  { "drop", 1, op_drop, "Stack", "x --", "drop the top stack element", "1 2 drop" },
  { "swap", 2, op_swap, "Stack", "a b -- b a", "swap top two elements", "1 2 swap" },
  { "over", 2, op_over, "Stack", "a b -- a b a", "copy second item to top", "1 2 over" },
  { "nip", 2, op_nip, "Stack", "a b -- b", "drop second-from-top element (keep top)", "1 2 nip" },
  { "tuck", 2, op_tuck, "Stack", "a b -- b a b", "duplicate second item and tuck below top", "1 2 tuck" },
  { "roll", 1, op_roll_n, "Stack", "... n -- ...", "move the item at depth n to the top (1 roll = swap, 2 roll = rot)", "1 2 3 2 roll" },
  { "pick", 1, op_pick, "Stack", "... n -- ... v", "copy the item at depth n to the top (0 pick = dup)", "1 2 3 2 pick" },
  { "depth", 0, op_depth, "Stack", "-- n", "number of items on the stack", "depth" },
  { "clst", 0, op_clst, "Stack", "... --", "clear the stack", "clst" },
  { "savestack", 0, op_savestack, "Stack", "--", "save the whole stack to stack_backup.dat in the config directory", "savestack" },
  { "loadstack", 0, op_loadstack, "Stack", "-- ...", "push the stack saved by savestack", "loadstack" },
  /* ---- Display ---- */
  { "print", 0, op_print, "Display", "x -- x", "print the top of the stack (a matrix in full)", "42 print" },
  { "pm", 1, op_pm, "Display", "A -- A", "print the matrix on top of the stack in full", "[2 2 $ 1 2 3 4] pm" },
  { "ps", 0, op_ps, "Display", "--", "print the whole stack", "ps" },
  { "setprec", 1, op_setprec, "Display", "n --", "digits after the decimal point (fixed) or significant digits (scientific), 0-20", "10 setprec" },
  { "sfs", 0, op_sfs, "Display", "--", "toggle between fixed-point and scientific display", "sfs" },
  /* ---- Words ---- */
  { "listwords", 0, op_listwords, "Words", "--", "list the user-defined words", "listwords" },
  { "listmacros", 0, op_listmacros, "Words", "--", "list the words of the loaded word modules", "listmacros" },
  { "loadwords", 0, op_loadwords, "Words", "--", "load the words saved in user_words.txt (also done at startup)", "loadwords" },
  { "savewords", 0, op_savewords, "Words", "--", "save all user-defined words to user_words.txt in the config directory", "savewords" },
  { "delword", 1, op_delword, "Words", "\"name\" --", "delete a user-defined word", "\"sq\" delword" },
  { "clrwords", 0, op_clrwords, "Words", "--", "delete all user-defined words", "clrwords" },
  { "use", 1, op_use, "Words", "\"name\" --", "load a word module: modules/name.mm in the config directory, or a path", "\"finance\" use" },
  { "unuse", 1, op_unuse, "Words", "\"name\" --", "unload a word module and its words", "\"finance\" unuse" },
  { "load", 1, op_load, "Session", "\"file.so\" --", "load a compiled module (a shared object exporting mm41_module); stays loaded", "\"hello.so\" load" },
  /* ---- Session ---- */
  { "eval", 1, op_eval, "Session", "s --", "evaluate the string s as a line of input", "\"1 2 +\" eval" },
  { "batch", 1, op_batch, "Session", "s --", "run the lines of file s as input", "\"calc.txt\" batch" },
  { "help", 0, op_help, "Session", "--", "print the quick-start help screen", "help" },
  { "usage", 1, op_usage, "Session", "\"name\" --", "show the stack picture, description and example of a command, or a word's definition", "\"sin\" usage" },
  { "listfcns", 0, op_listfcns, "Session", "--", "list all commands by category", "listfcns" },
  { "clrhist", 0, op_clrhist, "Session", "--", "clear the readline history", "clrhist" },
  { "fuck", 0, op_fuck, "Session", "--", "an Easter egg", "fuck" },
  { "modules", 0, op_modules, "Session", "--", "list the loaded modules with their versions", "modules" },
};

const Module mm41_mod_core = {
  "core", MM41_VERSION, "arithmetic, functions, logic, the stack, words and the session",
  core_table, (int)(sizeof core_table / sizeof core_table[0]), NULL, NULL, 0
};

int find_op(const char *name) {
  for (int i = 0; i < n_ops; i++)
    if (strcmp(ops[i].name, name) == 0) return i;
  return -1;
}

int call_op(Stack *stack, int idx) {
  const Op *op = &ops[idx];
  if (stack->top + 1 < op->nargs)
    return mm_error("%s: needs %d item%s on the stack (%s)", op->name, op->nargs,
                    op->nargs == 1 ? "" : "s", op->effect);
  return op->fn(stack);
}

/* Listings are grouped by module, then by the category each command declares. */
static int categories_of(const Module *m, const char **out, int max) {
  int n = 0, base = module_first_op(m);
  for (int i = base; i < base + m->n_ops; i++) {
    int seen = 0;
    for (int k = 0; k < n; k++) if (strcmp(out[k], ops[i].cat) == 0) { seen = 1; break; }
    if (!seen && n < max) out[n++] = ops[i].cat;
  }
  return n;
}

void list_ops_by_category(void) {
  for (int mi = 0; mi < modules_count(); mi++) {
    const Module *m = module_at(mi);
    const char *cats[32];
    int nc = categories_of(m, cats, 32), base = module_first_op(m);
    printf("[%s %s]\n", m->name, m->version);
    for (int c = 0; c < nc; c++) {
      printf("  %-18s", cats[c]);
      int col = 20;
      for (int i = base; i < base + m->n_ops; i++) {
        if (strcmp(ops[i].cat, cats[c]) != 0) continue;
        int l = (int)strlen(ops[i].name) + 1;
        if (col + l > 78) { printf("\n  %-18s", ""); col = 20; }
        printf(" %s", ops[i].name);
        col += l;
      }
      printf("\n");
    }
  }
  printf("'\"name\" usage' describes one command; user words: listwords; modules: modules.\n");
}

static void md(const char *s) {
  for (; *s; s++) { if (*s == '|') fputs("\\|", stdout); else putchar(*s); }
}

/* LaTeX: everything goes inside \texttt{} with the special characters escaped. */
static void tex(const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '\\': fputs("\\textbackslash{}", stdout); break;
    case '{': fputs("\\{", stdout); break;
    case '}': fputs("\\}", stdout); break;
    case '$': case '&': case '%': case '#': case '_': printf("\\%c", *s); break;
    case '^': fputs("\\^{}", stdout); break;
    case '~': fputs("\\~{}", stdout); break;
    case '|': fputs("\\textbar{}", stdout); break;
    case '<': fputs("\\textless{}", stdout); break;
    case '>': fputs("\\textgreater{}", stdout); break;
    case '"': fputs("\\textquotedbl{}", stdout); break;
    case '\'': fputs("\\textquotesingle{}", stdout); break;
    case '-': fputs("-{}", stdout); break;        /* no ligatures: -- stays -- */
    default: putchar(*s);
    }
  }
}

void print_ops_latex(void) {
  printf("%% Generated by mm41 --commands-tex from the dispatch table; do not edit by hand.\n");
  for (int mi = 0; mi < modules_count(); mi++) {
   const Module *m = module_at(mi);
   const char *cats[32];
   int nc = categories_of(m, cats, 32), base = module_first_op(m);
   printf("\\subsection*{Module \\texttt{%s} %s --- %s}\n", m->name, m->version, m->summary ? m->summary : "");
   for (int c = 0; c < nc; c++) {
    printf("\\subsubsection*{%s}\n", cats[c]);
    printf("\\begin{longtable}{@{}>{\\raggedright\\arraybackslash}p{0.12\\textwidth}>{\\raggedright\\arraybackslash}p{0.19\\textwidth}>{\\raggedright\\arraybackslash}p{0.36\\textwidth}>{\\raggedright\\arraybackslash}p{0.23\\textwidth}@{}}\n");
    printf("\\toprule \\textbf{Command} & \\textbf{Stack} & \\textbf{Description} & \\textbf{Example}\\\\ \\midrule \\endfirsthead\n");
    printf("\\toprule \\textbf{Command} & \\textbf{Stack} & \\textbf{Description} & \\textbf{Example}\\\\ \\midrule \\endhead\n");
    printf("\\bottomrule \\endlastfoot\n");
    for (int i = base; i < base + m->n_ops; i++) {
      if (strcmp(ops[i].cat, cats[c]) != 0) continue;
      printf("\\texttt{"); tex(ops[i].name); printf("} & ");
      if (ops[i].effect[0]) { printf("\\texttt{"); tex(ops[i].effect); printf("}"); }
      printf(" & "); tex(ops[i].help); printf(" & ");
      if (ops[i].example[0]) { printf("\\texttt{"); tex(ops[i].example); printf("}"); }
      printf("\\\\\n");
    }
    printf("\\end{longtable}\n\n");
   }
  }
}

void print_ops_markdown(void) {
  printf("# MM-41 command reference\n\n");
  printf("Generated by `mm41 --commands` from the dispatch table; do not edit by hand.\n");
  printf("Stack pictures read deepest-first: `y x -- y+x` consumes Y and X and leaves the sum in X.\n");
  for (int mi = 0; mi < modules_count(); mi++) {
   const Module *m = module_at(mi);
   const char *cats[32];
   int nc = categories_of(m, cats, 32), base = module_first_op(m);
   printf("\n# Module `%s` %s — %s\n", m->name, m->version, m->summary ? m->summary : "");
   for (int c = 0; c < nc; c++) {
    printf("\n## %s\n\n| Command | Stack | Description | Example |\n|---|---|---|---|\n", cats[c]);
    for (int i = base; i < base + m->n_ops; i++) {
      if (strcmp(ops[i].cat, cats[c]) != 0) continue;
      printf("| `"); md(ops[i].name); printf("` | ");
      if (ops[i].effect[0]) { printf("`"); md(ops[i].effect); printf("`"); }
      printf(" | "); md(ops[i].help); printf(" | ");
      if (ops[i].example[0]) { printf("`"); md(ops[i].example); printf("`"); }
      printf(" |\n");
    }
   }
  }
}
