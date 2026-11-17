/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Things that are not in the manual. */

#ifndef EGGS_H
#define EGGS_H

/* Feed every input line before evaluation; returns 1 when the line was
   taken and must not be evaluated. A word the user has defined, or a
   command, always wins over anything in here. */
int egg_line(const char *line);

#endif
