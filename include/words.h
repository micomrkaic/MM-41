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

#ifndef WORDS_H
#define WORDS_H

#include <stdbool.h>
#include "lexer.h"
#include "stack.h"

#define MAX_WORD_NAME   32
#define MAX_WORD_DEPTH  256     /* nesting of word calls before "runaway recursion" */
#define MAX_LOOP_NEST   16

/* A word is compiled when it is defined: its body is lexed once, control
   words become branches, identifiers are checked against the known words.
   Running a word is a loop over this code; no re-lexing. */
typedef enum {
  W_TOKEN,     /* an ordinary token, run through evaluate_one_token */
  W_BRANCH,    /* unconditional jump to target */
  W_BRANCH0,   /* pop flag; jump to target if it is 0 */
  W_DO,        /* pop start, limit; target = after the loop (used when start == limit) */
  W_LOOP,      /* index += 1; jump to target while index < limit */
  W_PLOOP,     /* pop n; index += n; loop while index has not crossed limit */
  W_I,         /* push innermost loop index */
  W_J          /* push the next outer loop index */
} winstr_kind;

typedef struct {
  winstr_kind kind;
  Token       tok;      /* W_TOKEN */
  int         target;   /* branches and loops */
} WInstr;

typedef struct {
  char    name[MAX_WORD_NAME];
  char   *body;         /* normalised source text, for listwords/savewords/usage */
  WInstr *code;
  int     ncode;
  bool    is_macro;     /* belongs to a word module (see below) */
  char    module[MAX_WORD_NAME]; /* owning word module, "" for user words */
} user_word;

/* Word modules: files of definitions (name body, or : name body ;) plugged
   in like HP-41 ROMs. Every *.mm in the modules/ directory of the config
   directory is loaded at startup; "name" use loads one by name or path,
   "name" unuse removes it. Their words live in the macro table, tagged
   with the module name, so clrwords leaves them alone. */
#define MAX_WORD_MODULES 64
typedef struct {
  char  name[MAX_WORD_NAME];
  char  version[16];
  char *summary;
  char *path;
  int   nwords;
} WordModule;
extern WordModule word_modules[];
extern int        word_module_count;

int  use_word_module(const char *name_or_path);   /* 0 on success */
int  unuse_word_module(const char *name);
int  autoload_word_modules(void);                 /* at startup */
void list_word_modules(void);                     /* part of `modules` */

extern user_word *words;
extern int word_count;
extern user_word *macros;
extern int macro_count;

/* Definitions: ": name body ;" (may be continued over lines by the caller). */
int  define_word(const char *name, const char *body, bool is_macro);
int  is_word_definition(const char *line);     /* 1 defined, -1 rejected, 0 not a definition */
bool is_control_word(const char *t);

user_word *find_word(const char *name);        /* user words first, then macros */
int  exec_word(Stack *stack, user_word *w);    /* honours MAX_WORD_DEPTH */

void list_words(void);
void list_macros(void);
void delete_word(Stack *stack);
void clear_words(void);
void free_words(void);
int  save_words_to_file(void);
int  load_words_from_file(void);
int  autoload_words(void);                     /* at startup: silent when there is no file */

#endif // WORDS_H
