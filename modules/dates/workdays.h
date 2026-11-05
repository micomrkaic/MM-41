/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Workdays and holiday calendars (dates module). */

#ifndef MM41_WORKDAYS_H
#define MM41_WORKDAYS_H

#include "stack.h"

int op_holidays(Stack *s);      /* name -- H */
int op_yholidays(Stack *s);     /* year name -- H */
int op_sethol(Stack *s);        /* H -- */
int op_usehol(Stack *s);        /* name -- */
int op_clrhol(Stack *s);        /* -- */
int op_hol(Stack *s);           /* -- H */
int op_workdays(Stack *s);      /* d1 d2 -- n */
int op_workdayplus(Stack *s);   /* date n -- date2 */
int op_isworkday(Stack *s);     /* date -- flag */

void workdays_shutdown(void);

#endif
