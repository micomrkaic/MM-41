/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#ifndef READ_FUN_H
#define READ_FUN_H

#include "stack.h"

int op_readmat(Stack *s);   /* "file" -- M */
int op_readvec(Stack *s);   /* "file" -- v */
int op_readcol(Stack *s);   /* "file" j -- v */
int op_getcol(Stack *s);    /* A j -- v */
int op_getrow(Stack *s);    /* A i -- v */

#endif
