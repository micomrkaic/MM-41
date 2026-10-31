/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#ifndef MM41_DEC_OPS_H
#define MM41_DEC_OPS_H

#include "stack.h"
#include <stdbool.h>

/* Decimal cases of the operators. The wrappers test dec_involved2() or
   dec_on_top() first; if true they call the apply function and return. */
bool dec_involved2(const Stack *stack);                 /* either of the top two is a decimal */
bool dec_on_top(const Stack *stack);
int  dec_binary_apply(Stack *stack, const char *op);    /* "+" "-" "*" "/" "^": y x -- r */
int  dec_compare_apply(Stack *stack, int *result);      /* -1 0 1, operands left in place */
int  dec_unary_apply(Stack *stack, const char *name);   /* in place on the top item */

#endif
