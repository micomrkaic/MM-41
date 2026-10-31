/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The decimal module: the number mode and conversions. The decimal type
   itself, its arithmetic and its display live in the core (decimal.c,
   dec_ops.c), because the operators must know about every type. */

#include "module.h"
#include "errors.h"
#include "stack.h"
#include "decimal.h"
#include "globals.h"
#include <math.h>

static int op_decimal(Stack *stack) { (void)stack; decimal_mode = true;  return 0; }
static int op_binary(Stack *stack)  { (void)stack; decimal_mode = false; return 0; }

/* x -- d : a real by its shortest round-trip form; a decimal unchanged */
static int op_to_dec(Stack *stack) {
  stack_element *e = &stack->items[stack->top];
  if (e->type == TYPE_DECIMAL) return 0;
  if (e->type != TYPE_REAL) return mm_error(">dec: only a real number converts to decimal");
  if (!isfinite(e->real)) return mm_error(">dec: %g has no decimal value", e->real);
  mm_dec d;
  dec_from_double(&d, e->real);
  e->type = TYPE_DECIMAL;
  e->dec = d;
  return 0;
}

/* d -- x : to the nearest double (34 digits become 16) */
static int op_to_bin(Stack *stack) {
  stack_element *e = &stack->items[stack->top];
  if (e->type == TYPE_REAL) return 0;
  if (e->type != TYPE_DECIMAL) return mm_error(">bin: only a decimal converts to binary");
  double x = dec_to_double(&e->dec);
  e->type = TYPE_REAL;
  e->real = x;
  return 0;
}

/* d n -- d' : round half-even to n decimal places (a real is converted first) */
static int op_round(Stack *stack) {
  double n;
  if (pop_real_arg(stack, "round", &n) != 0) return -1;
  if (n != floor(n) || n < -30 || n > 30) return mm_error("round: places must be an integer between -30 and 30");
  stack_element *e = &stack->items[stack->top];
  mm_dec d, r;
  if (e->type == TYPE_DECIMAL) d = e->dec;
  else if (e->type == TYPE_REAL) dec_from_double(&d, e->real);
  else return mm_error("round: needs a number");
  if (dec_round(&r, &d, (int)n) != 0) return -1;
  e->type = TYPE_DECIMAL;
  e->dec = r;
  return 0;
}

/* -- flag : 1 in decimal mode */
static int op_decmode(Stack *stack) { push_real(stack, decimal_mode ? 1.0 : 0.0); return 0; }

/* x -- flag : 1 if the top item is a decimal */
static int op_isdec(Stack *stack) {
  bool d = stack->items[stack->top].type == TYPE_DECIMAL;
  stack_element e = pop(stack); stack_element_free(&e);
  push_real(stack, d ? 1.0 : 0.0);
  return 0;
}

static const Op table[] = {
  { "decimal", 0, op_decimal, "Decimal", "--", "number mode: literals are decimals (34 digits, half-even) until `binary`", "decimal 0.1 0.2 +" },
  { "binary",  0, op_binary,  "Decimal", "--", "number mode: literals are binary doubles (the default)", "binary" },
  { ">dec",    1, op_to_dec,  "Decimal", "x -- d", "convert a real to a decimal by its shortest round-trip form (0.1 stays 0.1)", "0.1 >dec" },
  { ">bin",    1, op_to_bin,  "Decimal", "d -- x", "convert a decimal to a binary real (needed before matrices and the numerical methods)", "1d 3d / >bin" },
  { "round",   2, op_round,   "Decimal", "x n -- d", "round to n decimal places, half-even; the result is a decimal", "2.345d 2 round" },
  { "decmode", 0, op_decmode, "Decimal", "-- flag", "1 if the number mode is decimal", "decmode" },
  { "isdec",   1, op_isdec,   "Decimal", "x -- flag", "1 if x is a decimal", "1d isdec" },
};

const Module mm41_mod_decimal = {
  "decimal", "1.0", "decimal numbers for money: number mode, conversions, rounding",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
