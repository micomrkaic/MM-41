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
int  call_op(Stack *stack, int idx);              /* depth check + call */
void print_ops_markdown(void);                    /* --commands */
void print_ops_latex(void);                       /* --commands-tex */
void list_ops_by_category(void);                  /* listfcns */

#endif
