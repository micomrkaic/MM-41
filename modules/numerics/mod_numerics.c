/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#include "module.h"
#include "errors.h"
#include "stack.h"

#include "numerics.h"
#include "xt.h"
#include "globals.h"

static int op_exec(Stack *stack) { double xt; if (pop_real_arg(stack, "exec", &xt)) return -1; return xt_exec(stack, xt); }
static int op_tol(Stack *stack) {
  double t; if (pop_real_arg(stack, "tol", &t)) return -1;
  if (!(t > 0.0 && t < 1.0)) return mm_error("tol: tolerance must be between 0 and 1");
  num_tol = t; return 0;
}
static int op_nevals(Stack *stack) { push_real(stack, (double)num_evals); return 0; }
static int op_interr(Stack *stack) { push_real(stack, num_last_err); return 0; }
#define NUM3(nm, call) static int op_##nm(Stack *stack) {                      \
  double xt, b, a, r;                                                        \
  if (pop_real_arg(stack, #nm, &xt) || pop_real_arg(stack, #nm, &b) || pop_real_arg(stack, #nm, &a)) return -1; \
  if (call != 0) return -1;                                                  \
  push_real(stack, r); return 0; }
#define NUM2(nm, call) static int op_##nm(Stack *stack) {                      \
  double xt, a, r;                                                           \
  if (pop_real_arg(stack, #nm, &xt) || pop_real_arg(stack, #nm, &a)) return -1; \
  if (call != 0) return -1;                                                  \
  push_real(stack, r); return 0; }
static double err_dummy;
NUM3(integrate, num_integrate(stack, xt, a, b, &r, &err_dummy))
NUM3(romberg,   num_romberg(stack, xt, a, b, &r))
NUM3(fzero,     num_fzero(stack, xt, a, b, &r))
NUM2(solve,     num_solve(stack, xt, a, &r))
NUM3(minimize,  num_minimize(stack, xt, a, b, &r))
NUM2(deriv,     num_deriv(stack, xt, a, &r))
NUM3(fsum,      num_fsum(stack, xt, a, b, false, &r))
NUM3(fprod,     num_fsum(stack, xt, a, b, true, &r))



static const Op table[] = {
  { "exec", 1, op_exec, "Numerics", "xt --", "run an execution token (@name pushes one)", "3 @sq exec" },
  { "integrate", 3, op_integrate, "Numerics", "a b xt -- I", "integral of f over [a,b] (tanh-sinh with adaptive bisection; handles endpoint singularities)", "0 pi @sin integrate" },
  { "romberg", 3, op_romberg, "Numerics", "a b xt -- I", "integral of f over [a,b] by Romberg extrapolation", "0 1 @exp romberg" },
  { "fzero", 3, op_fzero, "Numerics", "a b xt -- x", "root of f in the bracket [a,b] where f changes sign (Brent)", "2 4 @sin fzero" },
  { "solve", 2, op_solve, "Numerics", "guess xt -- x", "root of f near one guess (bracket search, then Brent)", "3 @sin solve" },
  { "minimize", 3, op_minimize, "Numerics", "a b xt -- x", "argument of the minimum of f in [a,b] (Brent)", "0 5 @par minimize" },
  { "deriv", 2, op_deriv, "Numerics", "x xt -- f'(x)", "derivative of f at x (Ridders extrapolation)", "2 @exp deriv" },
  { "fsum", 3, op_fsum, "Numerics", "lo hi xt -- S", "sum of f(i) for integer i from lo to hi", "1 1000 @invsq fsum" },
  { "fprod", 3, op_fprod, "Numerics", "lo hi xt -- P", "product of f(i) for integer i from lo to hi", "1 10 @ident fprod" },
  { "tol", 1, op_tol, "Numerics", "x --", "set the convergence tolerance (default 1e-10)", "1e-6 tol" },
  { "nevals", 0, op_nevals, "Numerics", "-- n", "function evaluations used by the last numerical call", "nevals" },
  { "interr", 0, op_interr, "Numerics", "-- e", "error estimate of the last integrate", "interr" },
};

const Module mm41_mod_numerics = {
  "numerics", "1.0", "integration, roots, minima, derivatives and sums over any word",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
