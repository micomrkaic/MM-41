/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Decimal arithmetic on the stack: the operator wrappers call these first
   and return if a decimal was involved. "Decimal absorbs binary": a real
   operand is converted; anything else is an error. */

#include "dec_ops.h"
#include "errors.h"
#include "decimal.h"
#include <string.h>

/* Fetch an operand as a decimal: a decimal as is, a real converted. */
static int as_dec(const stack_element *e, mm_dec *out, const char *what) {
  if (e->type == TYPE_DECIMAL) { *out = e->dec; return 0; }
  if (e->type == TYPE_REAL)    { dec_from_double(out, e->real); return 0; }
  return mm_error("%s: a decimal combines only with a decimal or a real (convert with >bin)", what);
}

bool dec_involved2(const Stack *stack) {
  return stack->top >= 1 &&
         (stack->items[stack->top].type == TYPE_DECIMAL || stack->items[stack->top - 1].type == TYPE_DECIMAL);
}

bool dec_on_top(const Stack *stack) {
  return stack->top >= 0 && stack->items[stack->top].type == TYPE_DECIMAL;
}

int dec_binary_apply(Stack *stack, const char *op) {
  mm_dec a, b, r;
  if (as_dec(&stack->items[stack->top - 1], &a, op) != 0) return -1;
  if (as_dec(&stack->items[stack->top], &b, op) != 0) return -1;
  int rc;
  if      (!strcmp(op, "+")) rc = dec_add(&r, &a, &b);
  else if (!strcmp(op, "-")) rc = dec_sub(&r, &a, &b);
  else if (!strcmp(op, "*")) rc = dec_mul(&r, &a, &b);
  else if (!strcmp(op, "/")) rc = dec_div(&r, &a, &b);
  else if (!strcmp(op, "^")) rc = dec_pow(&r, &a, &b);
  else return mm_error("%s: not defined for decimals (convert with >bin)", op);
  if (rc != 0) return -1;
  stack->top -= 2;                         /* reals and decimals own no memory */
  push_decimal(stack, &r);
  return 0;
}

int dec_compare_apply(Stack *stack, int *result) {
  mm_dec a, b;
  if (as_dec(&stack->items[stack->top - 1], &a, "compare") != 0) return -1;
  if (as_dec(&stack->items[stack->top], &b, "compare") != 0) return -1;
  *result = dec_compare(&a, &b);
  return 0;
}

int dec_unary_apply(Stack *stack, const char *name) {
  mm_dec *a = &stack->items[stack->top].dec, r;
  int rc;
  if      (!strcmp(name, "chs"))  rc = dec_neg(&r, a);
  else if (!strcmp(name, "abs"))  rc = dec_abs(&r, a);
  else if (!strcmp(name, "inv"))  { mm_dec one; dec_from_long(&one, 1); rc = dec_div(&r, &one, a); }
  else if (!strcmp(name, "sqrt")) {
    if (dec_is_neg(a)) return mm_error("sqrt: negative decimal; convert with >bin for a complex result");
    rc = dec_sqrt(&r, a);
  }
  else if (!strcmp(name, "exp"))  rc = dec_exp(&r, a);
  else if (!strcmp(name, "ln")) {
    if (dec_is_neg(a) || dec_is_zero(a)) return mm_error("ln: decimal argument must be positive (convert with >bin)");
    rc = dec_ln(&r, a);
  }
  else if (!strcmp(name, "log")) {
    if (dec_is_neg(a) || dec_is_zero(a)) return mm_error("log: decimal argument must be positive (convert with >bin)");
    rc = dec_log10(&r, a);
  }
  else if (!strcmp(name, "intg")) rc = dec_intg(&r, a);
  else if (!strcmp(name, "frac")) { mm_dec i; dec_intg(&i, a); rc = dec_sub(&r, a, &i); }
  else return mm_error("%s: not defined for decimals (convert with >bin)", name);
  if (rc != 0) return -1;
  *a = r;
  return 0;
}
