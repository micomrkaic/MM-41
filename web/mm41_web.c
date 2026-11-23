/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* MM-41 in a browser: what the page calls.
 *
 * The calculator is the same program, compiled to WebAssembly with
 * Emscripten (make web). There is no terminal, so the page plays readline's
 * part: it shows what the calculator prints, reads a line, and hands it
 * over with mm41_web_line. The session is the one of include/session.h.
 * main() is still there, compiled as mm41_main, and is what the tests of
 * the web build call, to run the golden files in batch mode.
 *
 *   mm41_web_start()            once: the splash screen, modules, saved words
 *   mm41_web_prompt()           the prompt to show before the next line
 *   mm41_web_line(text)         run a line; 0 when the line was q
 *   mm41_web_complete(l, cur)   completions for the word that ends at cur
 *   mm41_web_main(args)         the command line: main() with the arguments
 *                               given one to a line; returns the exit status
 *
 * Everything the calculator prints goes through the Module's print and
 * printErr, which the page supplies.
 */

#define _POSIX_C_SOURCE 200809L
#include <emscripten.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_rng.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errors.h"
#include "globals.h"
#include "session.h"
#include "tab_completion.h"

EMSCRIPTEN_KEEPALIVE void mm41_web_start(void) {
  global_rng = gsl_rng_alloc(gsl_rng_mt19937);
  gsl_set_error_handler(&my_error_handler);
  mm41_session_start();
  fflush(stdout);
}

EMSCRIPTEN_KEEPALIVE const char *mm41_web_prompt(void) { return mm41_session_prompt(); }

EMSCRIPTEN_KEEPALIVE int mm41_web_line(const char *text) {
  char *line = strdup(text ? text : "");
  if (!line) return 1;
  bool go_on = mm41_session_line(line);
  free(line);
  save_config(CONFIG_PATH);          /* a page can be closed at any moment: there is no exit to save at */
  fflush(stdout);
  fflush(stderr);
  return go_on ? 1 : 0;
}

/* The completions of the word that ends at `cursor` in `line`, as text: the
   position where the word starts on the first line, then a candidate on
   each further line. The caller frees it. */
EMSCRIPTEN_KEEPALIVE char *mm41_web_complete(const char *line, int cursor) {
  int len = (int)strlen(line);
  if (cursor < 0 || cursor > len) cursor = len;
  int start = cursor;
  while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t' && line[start - 1] != '"') start--;
  char *text = strndup(line + start, (size_t)(cursor - start));
  char *copy = strdup(line);
  size_t cap = 64, used = 0;
  char *out = malloc(cap);
  if (!text || !copy || !out) { free(text); free(copy); free(out); return NULL; }
  used = (size_t)snprintf(out, cap, "%d", start);
  rl_line_buffer = copy;
  rl_attempted_completion_over = 0;
  char **m = function_name_completion(text, start, cursor);
  rl_line_buffer = "";
  if (m) {
    for (int i = 0; m[i]; i++) {
      if (i > 0) {                    /* m[0] is the common beginning */
        size_t l = strlen(m[i]);
        if (used + l + 2 > cap) {
          cap = 2 * (used + l + 2);
          char *bigger = realloc(out, cap);
          if (!bigger) { free(m[i]); continue; }
          out = bigger;
        }
        out[used++] = '\n';
        memcpy(out + used, m[i], l + 1);
        used += l;
      }
      free(m[i]);
    }
    free(m);
  }
  free(text);
  free(copy);
  return out;
}

/* ---- time zones: the offset of an IANA zone at local noon on d.m.y, from
   the browser's Intl (my_date_fun.c calls this instead of reading tzdata) ---- */

EM_JS(double, js_zone_offset, (const char *zone, int d, int m, int y), {
  try {
    var f = new Intl.DateTimeFormat('en-US', { timeZone: UTF8ToString(zone), hourCycle: 'h23',
      year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric', second: 'numeric' });
    var offset = function (t) {                  /* the zone's clock at the instant t, minus t */
      var p = {};
      f.formatToParts(new Date(t)).forEach(function (x) { p[x.type] = Number(x.value); });
      return Date.UTC(p.year, p.month - 1, p.day, p.hour % 24, p.minute, p.second) - t;
    };
    var noon = Date.UTC(y, m - 1, d, 12);       /* noon there is noon UTC less the offset, which depends on the instant */
    var o = offset(noon);
    o = offset(noon - o);
    return o / 3600000;
  } catch (e) {
    return NaN;
  }
});

int mm41_web_zone_offset(const char *zone, int d, int m, int y, double *hours_out) {
  double h = js_zone_offset(zone, d, m, y);
  if (h != h) {
    fprintf(stderr, "Error: unknown time zone \"%s\" (expected an IANA name such as \"Europe/Ljubljana\")\n", zone);
    return 0;
  }
  *hours_out = h;
  return 1;
}

/* main(), as `mm41 ARG ARG ...`: the arguments are the lines of `args`. */
int mm41_main(int argc, char **argv);

EMSCRIPTEN_KEEPALIVE int mm41_web_main(const char *args) {
  enum { MAXARGS = 64 };
  char *copy = strdup(args ? args : ""), *argv[MAXARGS + 2];
  if (!copy) return 2;
  int argc = 0;
  argv[argc++] = "mm41";
  for (char *p = copy; *p && argc < MAXARGS; ) {
    argv[argc++] = p;
    char *nl = strchr(p, '\n');
    if (!nl) break;
    *nl = '\0';
    p = nl + 1;
  }
  argv[argc] = NULL;
  int rc = mm41_main(argc, argv);
  fflush(stdout);
  fflush(stderr);
  free(copy);
  return rc;
}
