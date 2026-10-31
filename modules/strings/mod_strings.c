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
#include "string_fun.h"


static int op_scon(Stack *stack) { (void)stack; (void)(concatenate(stack)); return 0; }
static int op_substr(Stack *stack) { (void)stack; (void)(my_substring(stack)); return 0; }
static int op_s2l(Stack *stack) { (void)stack; (void)(to_lower(stack)); return 0; }
static int op_s2u(Stack *stack) { (void)stack; (void)(to_upper(stack)); return 0; }
static int op_slen(Stack *stack) { (void)stack; (void)(string_length(stack)); return 0; }
static int op_srev(Stack *stack) { (void)stack; (void)(string_reverse(stack)); return 0; }
static int op_int2str(Stack *stack) { (void)stack; (void)(top_to_string(stack)); return 0; }

static const Op table[] = {
  { "scon", 2, op_scon, "Strings", "s1 s2 -- s1s2", "concatenate two strings", "\"foo\" \"bar\" scon" },
  { "substr", 3, op_substr, "Strings", "s start end -- sub", "substring from index start (0-based) up to end (exclusive)", "\"abcdef\" 1 3 substr" },
  { "s2l", 1, op_s2l, "Strings", "s -- s_lower", "convert string to lowercase", "\"Hello\" s2l" },
  { "s2u", 1, op_s2u, "Strings", "s -- s_upper", "convert string to uppercase", "\"Hello\" s2u" },
  { "slen", 1, op_slen, "Strings", "s -- n", "length of a string in bytes", "\"hello\" slen" },
  { "srev", 1, op_srev, "Strings", "s -- s_rev", "reverse string", "\"abc\" srev" },
  { "int2str", 1, op_int2str, "Strings", "x -- s", "integer part of x as a string", "123 int2str" },
};

const Module mm41_mod_strings = {
  "strings", "1.0", "string construction and manipulation",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
