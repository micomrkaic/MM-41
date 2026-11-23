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

#define _POSIX_C_SOURCE 200809L

#include "errors.h"
#include "eval_fun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <readline/history.h>
#include <gsl/gsl_complex.h>        // for gsl_complex
#include <gsl/gsl_math.h>           // for M_PI
#include <stdbool.h>                // for true
#include "lexer.h"
#include "stack.h"
#include "stack_io.h"
#include "math_parsers.h"
#include "math_helpers.h"
#include "binary_fun.h"
#include "unary_fun.h"
#include "help.h"
#include "print_fun.h"
#include "compare_fun.h"
#include "words.h"
#include "globals.h"
#include "ops.h"
#include "xt.h"

// **************** The main loop in this file ****************
/* Each top-level token is atomic: a snapshot of the stack is taken before
   it runs, and if the token reports an error the stack is restored and the
   rest of the line is skipped. A user word is one token for this purpose. */
void evaluate_line(Stack *stack, char* line) {
  int def = is_word_definition(line);
  if (def != 0) {
    // definition line (accepted or rejected) is already handled; do not lex it
    return;
  }

  /* the snapshots are this function's own business: if one cannot be made
     that is an ordinary error here, not a jump to whichever command, further
     out, happens to be running this line */
  jmp_buf *outer = mm_nomem_escape;
  mm_nomem_escape = NULL;
  Stack before;
  init_stack(&before);
  Lexer lexer = (Lexer){ .input = line, .pos = 0 };
  Token tok;
  int warned = 0;
  do {
    tok = next_token(&lexer);
    if (tok.type == TOK_EOF) break;
    bool saved = snapshot_stack(&before, stack);
    if (!saved && !warned++)
      mm_warn("out of memory: no room to save the stack before '%.40s'; if it fails the stack stays as it leaves it. drop or clst to make room", tok.text);
    long errors_before = error_count;
    evaluate_one_token(stack, &tok);
    if (error_count > errors_before) {
      if (saved) move_stack(stack, &before);       /* a move: restoring allocates nothing */
      break;
    }
  } while (1);
  free_stack(&before);
  mm_nomem_escape = outer;
}

/* void evaluate_line(Stack *stack, char* line) { */
/*   Lexer lexer = {line, 0}; */
/*   Token tok; */

/*   if (!is_word_definition(line))   // Check if a new word; insert  if it is */
/*     do {   // The lexer loop */
/*       tok = next_token(&lexer); */
/*       evaluate_one_token(stack, tok);     */
/*     } while (tok.type != TOK_EOF); */
/* } */

// **************** Process one token ****************
static void run_token(Stack *stack, const Token *tok);

/* One token, with the recovery point for running out of memory around it
   (errors.h). The jump comes from the GSL error handler or from xmalloc,
   somewhere below run_token; the error it turns into is handled by the
   caller like any other: the stack goes back to what it was. */
void evaluate_one_token(Stack *stack, const Token *tok) {
  jmp_buf here;
  jmp_buf *volatile outer = mm_nomem_escape;
  mm_nomem_escape = &here;
  if (setjmp(here) == 0) {
    run_token(stack, tok);
    mm_nomem_escape = outer;
    return;
  }
  mm_nomem_escape = outer;
  gsl_set_error_handler(&my_error_handler);      /* the command may have had it switched off */
  mm_error("out of memory: '%.40s' was abandoned", tok->text);
}

static void run_token(Stack *stack, const Token *tok) {
  switch (tok->type) {
  case TOK_EOF:
    return;
  case TOK_NUMBER: {
    /* "0.1" follows the number mode; "0.1d" is always decimal, "0.1b" always binary */
    size_t n = strlen(tok->text);
    char last = n ? tok->text[n - 1] : 0;
    bool dec = (last == 'd') || (decimal_mode && last != 'b');
    char buf[256];                                   /* a number, not a matrix: short */
    snprintf(buf, sizeof buf, "%.*s", (int)(last == 'd' || last == 'b' ? n - 1 : n), tok->text);
    if (dec) {
      mm_dec d;
      if (dec_from_string(&d, buf) != 0) { mm_error("not a decimal number: %s", buf); return; }
      push_decimal(stack, &d);
    } else {
      push_real(stack, atof(buf));
    }
    return;
  }
  case TOK_COMPLEX: {
    gsl_complex z;
    if (read_complex(tok->text, &z)) push_complex(stack,z);
    return;}
  case TOK_STRING:
    push_string(stack, tok->text);
    return;
  case TOK_MATRIX_FILE:
    read_matrix_from_file(stack, tok->text);
    return;
  case TOK_MATRIX_INLINE_REAL: {                   /* a literal that does not parse has said why: push nothing */
    gsl_matrix *m = parse_matrix_literal(tok->text);
    if (m) push_matrix_real(stack, m);
    return;
  }
  case TOK_MATRIX_INLINE_COMPLEX:
  case TOK_MATRIX_INLINE_MIXED: {
    gsl_matrix_complex *m = parse_complex_matrix_literal(tok->text);
    if (m) push_matrix_complex(stack, m);
    return;
  }
  case TOK_PLUS: case TOK_MINUS: case TOK_STAR: case TOK_SLASH: case TOK_CARET:
  case TOK_DOT_STAR: case TOK_DOT_SLASH: case TOK_DOT_CARET: {
    int idx = find_op(tok->text);
    if (idx >= 0) call_op(stack, idx);
    else mm_error("Unknown operator: %s", tok->text);
    return;
  }
  case TOK_BRA: case TOK_KET: case TOK_VERTICAL:
    mm_error("Illegal token: %s", tok->text);
    return;
  case TOK_COLON:
    mm_error("':' starts a word definition: ': name body ;' on one line");
    return;
  case TOK_SEMICOLON:
    mm_error("';' without a matching ':'");
    return;
  case TOK_IDENTIFIER: {
    user_word *w = find_word(tok->text);
    if (!w) { mm_error("Unknown word: %s", tok->text); return; }
    exec_word(stack, w);
    return;
  }
  case TOK_FUNCTION: {

    int idx = find_op(tok->text);
    if (idx >= 0) call_op(stack, idx);
    else mm_error("Unknown command: %s", tok->text);
    return;
  }
  case TOK_XT: {
    double xt = xt_of(tok->text + 1);
    if (xt < 0) { mm_error("%s: no such command or word", tok->text); return; }
    push_real(stack, xt);
    return;
  }
  case TOK_UNKNOWN:
    mm_error("Illegal token: %s", tok->text);
    return;
  default:
    mm_error("Unhandled token: %s", tok->text);
    return;
  }
}

/* Run the lines of a file as input (the `batch` command and -f). */
int run_batch(Stack *stack, char *fname) {
  FILE* f = fopen(fname, "r");
  if (!f) {
    mm_perror("Failed to open input file");
    return 1;
  } else if (verbose_mode) printf("Running batch: %s\n",fname);

  char *line = NULL;
  size_t len = 0;

  while (getline(&line, &len, f) != -1) {
    // Remove trailing newline
    line[strcspn(line, "\r\n")] = '\0';
    evaluate_line(stack, line);
  }
  free(line);
  fclose(f);
  completed_batch = true;
  return 0;
}

