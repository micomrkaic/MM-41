/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#ifndef MANUAL_H
#define MANUAL_H

void manual_open(const char *topic);      /* NULL or "": from the top; a chapter title, number or a word */
int  manual_titles(char **out, int cap);  /* chapter titles, lower-cased and strdup'd, for completion */

#endif
