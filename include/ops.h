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

#ifndef OPS_H
#define OPS_H

#include <math.h>
#include "stack.h"

/* The dispatch table: one row per built-in command. It drives dispatch,
   the minimum-depth check, 'usage', 'listfcns', tab completion and the
   generated command reference. Adding a command is adding a row. */
typedef int (*OpFn)(Stack *stack);

typedef struct {
  const char *name;
  int         nargs;     /* minimum stack depth required */
  OpFn        fn;
  const char *cat;
  const char *effect;    /* stack picture, e.g. "y x -- y+x" */
  const char *help;
  const char *example;
} Op;

/* The flat table assembled from all loaded modules (see module.h). */
extern const Op *ops;
extern int       n_ops;

int  find_op(const char *name);                   /* index or -1 */

/* argument helpers for command implementations */
int pop_string_arg(Stack *stack, const char *what, char **out);   /* caller frees *out */
int pop_real_arg(Stack *stack, const char *what, double *out);
int pop_two_reals(Stack *stack, const char *what, double *y, double *x);

/* Numbers from the stack that are to be used as counts, indices or sizes.
 * Converting a double that is nan, infinite or out of range to an integer
 * type is undefined behaviour in C, and a negative count read as a size_t is
 * a request for 16 exabytes; every such number goes through these.
 *   real_to_long  x truncated toward zero, if it is a number in [lo, hi];
 *                 otherwise an error naming `what` and the range, and -1.
 *   pop_long_arg  the same for the number on top of the stack, which it pops.
 *   matrix_dims_ok  rows and cols of a matrix that can be allocated: both at
 *                 least 1, and no more than MM41_MAX_CELLS cells in all.
 *   whole_in      is x a whole number in [lo, hi]? It makes no conversion,
 *                 so it is the test to make before one: the usual
 *                 "x != (int)x" already converts, and is undefined for the
 *                 very values it is meant to reject. */
#define MM41_MAX_CELLS 16000000L                  /* 128 MB of doubles: beyond a calculator's business */
#define MM41_MAX_COUNT 10000000L                  /* draws, steps, terms: more than this is a typing error */
int real_to_long(double x, long lo, long hi, const char *what, long *out);
int pop_long_arg(Stack *stack, const char *what, long lo, long hi, long *out);
int matrix_dims_ok(double rows, double cols, const char *what);
static inline int whole_in(double x, double lo, double hi) { return x >= lo && x <= hi && x == floor(x); }
int  call_op(Stack *stack, int idx);              /* depth check + call */
void print_ops_markdown(void);                    /* --commands */
void print_ops_latex(void);                       /* --commands-tex */
void list_ops_by_category(void);                  /* listfcns */

#endif
