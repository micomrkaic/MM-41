/* Readline's side of web/shim/readline/readline.h. The history lives in the
   page (it keeps it in the browser's storage); completion is real. */
#define _POSIX_C_SOURCE 200809L
#include "readline/readline.h"
#include <stdlib.h>
#include <string.h>

char *rl_line_buffer = "";
int rl_attempted_completion_over = 0;
rl_completion_func_t *rl_attempted_completion_function = 0;

char *readline(const char *prompt) { (void)prompt; return NULL; }
void add_history(const char *line) { (void)line; }
int  read_history(const char *file) { (void)file; return 0; }
int  write_history(const char *file) { (void)file; return 0; }
void stifle_history(int max) { (void)max; }
void clear_history(void) { }

/* As readline's: NULL when nothing matches; otherwise [0] is what all the
   matches begin with and the matches follow, NULL-terminated. */
char **rl_completion_matches(const char *text, rl_compentry_func_t *generator) {
  char **v = NULL, *m;
  int n = 0, cap = 0;
  for (int state = 0; (m = generator(text, state)) != NULL; state = 1) {
    if (n + 3 > cap) {
      cap = cap ? 2 * cap : 16;
      char **nv = realloc(v, (size_t)cap * sizeof *nv);
      if (!nv) { free(m); break; }
      v = nv;
    }
    v[++n] = m;
  }
  if (n == 0) { free(v); return NULL; }
  size_t common = strlen(v[1]);
  for (int i = 2; i <= n; i++) {
    size_t k = 0;
    while (k < common && v[i][k] == v[1][k]) k++;
    common = k;
  }
  v[0] = strndup(v[1], common);
  v[n + 1] = NULL;
  return v;
}
