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

#ifndef NUMERICS_H
#define NUMERICS_H

#include <stdbool.h>
#include "stack.h"

/* Numerical methods over user functions. A function is an execution token,
   pushed by @name, for a built-in or a word that maps x on the stack to
   f(x) on the stack. The function runs on the live stack, so words may use
   registers and other words; an error inside it aborts the call and the
   top-level rollback restores the stack. Convergence uses num_tol. */

#define MAX_FN_EVALS  2000000

/* num_tol, num_evals and num_last_err live in globals.h (tol is saved in the config). */

int num_eval(Stack *stack, double xt, double x, double *fx);
int num_integrate(Stack *stack, double xt, double a, double b, double *result, double *err);
int num_romberg(Stack *stack, double xt, double a, double b, double *result);
int num_fzero(Stack *stack, double xt, double a, double b, double *root);
int num_solve(Stack *stack, double xt, double guess, double *root);
int num_minimize(Stack *stack, double xt, double a, double b, double *xmin);
int num_deriv(Stack *stack, double xt, double x, double *dfdx);
int num_fsum(Stack *stack, double xt, double lo, double hi, bool product, double *result);

#endif
