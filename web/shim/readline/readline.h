/* The few names of GNU Readline that MM-41 uses, for the browser build:
   there is no terminal there, the page reads the lines and asks for
   completions itself (web/mm41_web.c). */
#ifndef MM41_WEB_READLINE_H
#define MM41_WEB_READLINE_H

typedef char *rl_compentry_func_t(const char *, int);
typedef char **rl_completion_func_t(const char *, int, int);

extern char *rl_line_buffer;
extern int rl_attempted_completion_over;
extern rl_completion_func_t *rl_attempted_completion_function;

char *readline(const char *prompt);
char **rl_completion_matches(const char *text, rl_compentry_func_t *generator);

#include "history.h"

#endif
