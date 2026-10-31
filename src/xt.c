/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */


/* Execution tokens: @name pushes a real that identifies a command or word. */

#include "errors.h"
#include <math.h>
#include <string.h>
#include "ops.h"
#include "words.h"
#include "xt.h"

/* ======================= execution tokens ======================= */
/* Encoding: built-in op i -> i; user word i -> XT_WORD + i; macro i -> XT_MACRO + i. */
#define XT_WORD  1000000.0
#define XT_MACRO 2000000.0

double xt_of(const char *name) {
  int op = find_op(name);
  if (op >= 0) return (double)op;
  for (int i = 0; i < word_count; i++) if (!strcmp(words[i].name, name)) return XT_WORD + i;
  for (int i = 0; i < macro_count; i++) if (!strcmp(macros[i].name, name)) return XT_MACRO + i;
  return -1.0;
}

static int xt_decode(double xt, int *op, user_word **w) {
  *op = -1; *w = NULL;
  if (xt != floor(xt) || xt < 0) return mm_error("%g is not an execution token (use @name)", xt);
  if (xt < n_ops) { *op = (int)xt; return 0; }
  if (xt >= XT_MACRO && xt < XT_MACRO + macro_count) { *w = &macros[(int)(xt - XT_MACRO)]; return 0; }
  if (xt >= XT_WORD && xt < XT_WORD + word_count) { *w = &words[(int)(xt - XT_WORD)]; return 0; }
  return mm_error("%g is not an execution token (use @name)", xt);
}

const char *xt_name(double xt) {
  if (xt != floor(xt) || xt < 0) return "?";
  if (xt < n_ops) return ops[(int)xt].name;
  if (xt >= XT_MACRO && xt < XT_MACRO + macro_count) return macros[(int)(xt - XT_MACRO)].name;
  if (xt >= XT_WORD && xt < XT_WORD + word_count) return words[(int)(xt - XT_WORD)].name;
  return "?";
}

int xt_check(double xt) { int op; user_word *w; return xt_decode(xt, &op, &w); }

int xt_exec(Stack *stack, double xt) {
  int op; user_word *w;
  if (xt_decode(xt, &op, &w) != 0) return -1;
  return op >= 0 ? call_op(stack, op) : exec_word(stack, w);
}

