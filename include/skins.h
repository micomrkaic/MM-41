/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Display skins: the stack drawn the way a classic calculator would show
   it. `plain` is MM-41's own listing; hp41 is one twelve-character LCD
   line with annunciators, hp12c a seven-segment display, hp48 a dot-matrix
   screen in block pixels. The skin only changes what is printed after a
   line; pm, help, tables and errors are untouched. */

#ifndef SKINS_H
#define SKINS_H

#include "stack.h"

enum { SKIN_PLAIN, SKIN_HP41, SKIN_HP12C, SKIN_HP48, SKIN_HP48PX, SKIN_COUNT };

extern int skin;                               /* saved in config.txt as skin = name */
extern int ann_begin;                          /* finance: payments at the beginning (the 12C's BEGIN) */

const char *skin_name(int s);                  /* "plain" "hp41" "hp12c" "hp48" "hp48px" */
int         skin_from_name(const char *name);  /* -1 if unknown */
void        skin_print_stack(const Stack *s);  /* the stack in the current skin (not plain) */

int op_skin(Stack *s);                         /* "name" skin */

/* The hidden part. Feed every input line; returns 1 when the line was taken
   and must not be evaluated. */
int skin_secret_line(const char *line);

#endif
