/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The tutorial: a book of lessons you type along with (tutor), and the
   same book printed as a cookbook (examples). */

#ifndef MM41_TUTOR_H
#define MM41_TUTOR_H

#include "stack.h"
#include <stdbool.h>

/* Line forms: `tutor`, `tutor 3`, `tutor list`, `tutor reset`;
   `examples`, `examples 3`, `examples finance`. */
void tutor_line(const char *args, Stack *stack);
void examples_line(const char *args);

/* While a lesson is open: the tutor's own commands (next skip back again q)
   are taken here, before the line is evaluated; returns true if it was one.
   tutor_after runs after every line and checks the stack against the lesson. */
bool tutor_active(void);
bool tutor_command(const char *line, Stack *stack);
void tutor_after(Stack *stack);
const char *tutor_prompt(void);

/* Chapter titles (lower case, the first word) for tab completion: fills `out`
   with up to `cap` strdup'd names, returns the count. */
int tutor_titles(char **out, int cap);

/* Progress, kept in config.txt as "tutor = chapter.step". */
extern char tutor_progress[32];

/* --tutor-check: run every lesson's lines through `run` (the REPL's own line
   handler) against a fresh stack; 0 if all expectations hold. */
int tutor_self_check(bool (*run)(Stack *stack, char *line));

#endif
