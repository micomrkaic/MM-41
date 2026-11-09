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

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <readline/readline.h>  // for rl_completion_matches, rl_line_buffer
#include <stdio.h>
#include <stdlib.h>
#include <string.h>             // for strdup, strncmp, NULL, strlen
#include "globals.h"            // for app_path
#include "ops.h"                // for ops, n_ops
#include "tab_completion.h"     // for function_name_completion
#include "words.h"              // for user_word, word_modules

/* Names of the files in the config directory's `sub` folder that end in
   `ext`, without the extension; collected once per completion. */
static char **dir_names(const char *sub, const char *ext, int *count) {
  *count = 0;
  DIR *d = opendir(app_path(sub));
  if (!d) return NULL;
  char **v = NULL; int cap = 0;
  struct dirent *e;
  size_t el = strlen(ext);
  while ((e = readdir(d)) != NULL) {
    size_t l = strlen(e->d_name);
    if (l <= el || strcmp(e->d_name + l - el, ext) != 0) continue;
    if (*count == cap) { cap = cap ? 2 * cap : 16; v = realloc(v, (size_t)cap * sizeof *v); }
    v[(*count)++] = strndup(e->d_name, l - el);
  }
  closedir(d);
  return v;
}

static char **names; static int n_names, name_index;

static char *name_generator(const char *text, int state) {
  if (!state) name_index = 0;
  size_t len = strlen(text);
  while (name_index < n_names) {
    const char *c = names[name_index++];
    if (strncmp(c, text, len) == 0) return strdup(c);
  }
  return NULL;
}

static void free_names(void) {
  for (int i = 0; i < n_names; i++) free(names[i]);
  free(names); names = NULL; n_names = 0;
}

/* After `use `: the word modules in the config directory. After `unuse `:
   the ones loaded. After `usehol `: the rule sets and the calendar files.
   After load, run and batch: file names (readline's own completion). */
static char **argument_completion(const char *word, size_t wl, const char *text, int *handled) {
  free_names();
  *handled = 1;
  if (!strncmp(word, "use", wl) && wl == 3) {
    names = dir_names("modules", ".mm", &n_names);
  } else if (!strncmp(word, "unuse", wl) && wl == 5) {
    names = calloc((size_t)word_module_count + 1, sizeof *names);
    for (int i = 0; i < word_module_count; i++) names[n_names++] = strdup(word_modules[i].name);
  } else if (!strncmp(word, "usehol", wl) && wl == 6) {
    int nf; char **f = dir_names("holidays", ".txt", &nf);
    names = calloc((size_t)nf + 2, sizeof *names);
    names[n_names++] = strdup("us"); names[n_names++] = strdup("si");
    for (int i = 0; i < nf; i++) names[n_names++] = f[i];
    free(f);
  } else if ((!strncmp(word, "load", wl) && wl == 4) || (!strncmp(word, "run", wl) && wl == 3) ||
             (!strncmp(word, "batch", wl) && wl == 5)) {
    rl_attempted_completion_over = 0;                   /* let readline complete a file name */
    return NULL;
  } else {
    *handled = 0;
    return NULL;
  }
  rl_attempted_completion_over = 1;
  return rl_completion_matches(text, name_generator);
}

char* function_name_generator(const char* text, int state) {
  static int phase;  // 0 = builtins, 1 = user words, 2 = macros
  static int index;
  static int len;


  if (!state) {
    phase = 0;
    index = 0;
    len = strlen(text);
  }

  while (phase < 3) {
    const char* candidate = NULL;
    (void) candidate;

    switch (phase) {
    case 0:
      while (index < n_ops) {
	const char* fn = ops[index++].name;
	if (strncmp(fn, text, len) == 0) {
	  return strdup(fn);
	}
      }
      break;

    case 1:
      while (index < word_count) {
	const char* name = words[index++].name;
	if (strncmp(name, text, len) == 0) {
	  return strdup(name);
	}
      }
      break;

    case 2:
      while (index < macro_count) {
	const char* name = macros[index++].name;
	if (strncmp(name, text, len) == 0) {
	  return strdup(name);
	}
      }
      break;
    }

    // Advance to next phase
    phase++;
    index = 0;
  }

  return NULL;
}

char** function_name_completion(const char* text, int start, int end) {
  (void)end;
  /* `use co<TAB>`: the first word of the line takes a name -- complete that */
  {
    const char *p = rl_line_buffer;
    while (*p == ' ' || *p == '\t') p++;
    const char *e = p;
    while (*e && *e != ' ' && *e != '\t') e++;
    if (*e && p + (e - p) < rl_line_buffer + start) {
      const char *a = e;
      while (*a == ' ' || *a == '\t') a++;
      if (a == rl_line_buffer + start) {
        int handled;
        char **m = argument_completion(p, (size_t)(e - p), text, &handled);
        if (handled) return m;
      }
    }
  }
  // Complete only if at the beginning of the line or after space
  if (start == 0 || rl_line_buffer[start - 1] == ' ') {
    return rl_completion_matches(text, function_name_generator);
  }
  return NULL;
}


