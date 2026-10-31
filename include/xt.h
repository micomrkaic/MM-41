/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */


#ifndef MM41_XT_H
#define MM41_XT_H

#include "stack.h"

/* Execution tokens. @name pushes one; the numerical methods and `exec`
   consume them. Encoding: built-in op i -> i; user word i -> 1e6 + i;
   macro i -> 2e6 + i. */
double      xt_of(const char *name);     /* -1 if unknown */
const char *xt_name(double xt);
int         xt_check(double xt);         /* error unless valid */
int         xt_exec(Stack *stack, double xt);

#endif
