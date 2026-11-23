/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The interactive session in steps (main.c): start, then a prompt and a
 * line at a time, then end. repl() is these around readline; the browser
 * build (web/) calls them from the page. */

#ifndef MM41_SESSION_H
#define MM41_SESSION_H

#include <stdbool.h>

void mm41_session_start(void);              /* the splash screen, the modules, the saved words and config */
const char *mm41_session_prompt(void);      /* "MM-41>> ", or the tutor's */
bool mm41_session_line(char *line);         /* run a line and show the stack; false when the line was q */
void mm41_session_end(void);                /* save the config, free everything */

#endif
