/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The manual from inside the calculator. `manual` opens the plain-text
   rendering of the user manual (doc/mm41-manual.txt, installed next to
   the PDF) in the pager; `manual survival` opens it at that chapter, and
   `manual npv` at the first line mentioning npv. The pager is $PAGER, then
   less, then more; with none of them, or no terminal, the chapter or the
   matching region is printed. */

#define _POSIX_C_SOURCE 200809L
#include "manual.h"
#include "errors.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MM41_DOCDIR
#define MM41_DOCDIR "/usr/local/share/doc/mm41"
#endif

/* Where the text manual is: $MM41_MANUAL, the source tree, the install
   directory, the usual system places. */
static const char *manual_path(void) {
  static char path[4096];
  const char *env = getenv("MM41_MANUAL");
  const char *candidates[] = { env, "doc/mm41-manual.txt", MM41_DOCDIR "/mm41-manual.txt",
                               "/usr/local/share/doc/mm41/mm41-manual.txt", "/usr/share/doc/mm41/mm41-manual.txt",
                               "/opt/homebrew/share/doc/mm41/mm41-manual.txt" };
  for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
    if (!candidates[i] || !candidates[i][0]) continue;
    if (access(candidates[i], R_OK) == 0) { snprintf(path, sizeof path, "%s", candidates[i]); return path; }
  }
  return NULL;
}

/* case-insensitive strstr */
static const char *ci_find(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  for (const char *p = hay; *p; p++) if (strncasecmp(p, needle, n) == 0) return p;
  return NULL;
}

static int is_heading(const char *line) {
  const char *p = line;
  if (!isdigit((unsigned char)*p)) return 0;
  while (isdigit((unsigned char)*p) || *p == '.') p++;
  return p[0] == ' ' && p[1] == ' ';
}

/* The line number (1-based) of the chapter whose title matches `topic`,
   or of the first line mentioning it; 0 if nothing matches. A chapter
   number ("9", "9.3") also works. */
static long find_line(const char *path, const char *topic, int *is_chapter) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  char line[4096];
  long n = 0, first_mention = 0, heading = 0;
  int past_contents = 0;
  size_t tl = strlen(topic);
  int numeric = 1;
  for (const char *p = topic; *p; p++) if (!isdigit((unsigned char)*p) && *p != '.') numeric = 0;
  while (fgets(line, sizeof line, f)) {
    n++;
    if (!past_contents) { if (n > 6 && is_heading(line)) past_contents = 1; else continue; }
    if (is_heading(line)) {
      const char *title = line;
      while (isdigit((unsigned char)*title) || *title == '.') title++;
      while (*title == ' ') title++;
      if (numeric) {
        if (strncmp(line, topic, tl) == 0 && line[tl] == ' ') { heading = n; break; }
      } else if (ci_find(title, topic) == title || ci_find(title, topic)) {
        if (!heading || ci_find(title, topic) == title) { heading = n; if (ci_find(title, topic) == title) break; }
      }
    }
    if (!first_mention && !numeric && ci_find(line, topic)) first_mention = n;
  }
  fclose(f);
  *is_chapter = heading != 0;
  return heading ? heading : first_mention;
}

/* Print from `from` (1-based) to the end of that chapter, or `lines` lines. */
static void print_region(const char *path, long from, int whole_chapter) {
  FILE *f = fopen(path, "r");
  if (!f) return;
  char line[4096];
  long n = 0, printed = 0;
  int level1 = 0;
  while (fgets(line, sizeof line, f)) {
    n++;
    if (n < from) continue;
    if (whole_chapter && n > from && is_heading(line)) {
      /* stop at the next heading of the same or higher level */
      const char *p = line; int dots = 0;
      while (isdigit((unsigned char)*p) || *p == '.') { if (*p == '.') dots++; p++; }
      if (dots <= level1) break;
    } else if (whole_chapter && n == from) {
      const char *p = line; while (isdigit((unsigned char)*p) || *p == '.') { if (*p == '.') level1++; p++; }
    }
    if (!whole_chapter && printed >= 40) { printf("  ... (in a terminal, manual opens the whole text in the pager)\n"); break; }
    fputs(line, stdout);
    printed++;
  }
  fclose(f);
}

/* Run the pager on the manual: at line `at` (1-based) for a chapter, or
   searching for `pattern` so that n in less finds the next mention. */
static int run_pager(const char *path, long at, const char *pattern) {
  const char *pager = getenv("PAGER");
  if (!pager || !*pager) pager = "less";
  char plus[300];
  if (pattern) snprintf(plus, sizeof plus, "+/%s", pattern);
  else snprintf(plus, sizeof plus, "+%ld", at > 0 ? at : 1);
  fflush(stdout);
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    if (strcmp(pager, "less") == 0 || strstr(pager, "/less")) {
      execlp(pager, pager, "-R", "-i", plus, path, (char *)NULL);
      execlp("less", "less", "-R", "-i", plus, path, (char *)NULL);
    } else {
      execlp(pager, pager, path, (char *)NULL);
    }
    execlp("more", "more", plus, path, (char *)NULL);
    _exit(127);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) != 127 ? 0 : -1;
}

void manual_open(const char *topic) {
  const char *path = manual_path();
  if (!path) {
    mm_error("manual: mm41-manual.txt not found (make docs renders it; make install puts it in %s); the PDF is doc/mm41-manual.pdf", MM41_DOCDIR);
    return;
  }
  while (topic && (*topic == ' ' || *topic == '\t')) topic++;
  long at = 0; int chapter = 0;
  if (topic && *topic) {
    char t[256]; snprintf(t, sizeof t, "%s", topic);
    for (char *e = t + strlen(t); e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n'); e--) e[-1] = '\0';
    at = find_line(path, t, &chapter);
    if (!at) { mm_error("manual: nothing about '%s' in the manual; help %s searches the commands", t, t); return; }
  }
  char pattern[256] = "";
  if (at && !chapter) snprintf(pattern, sizeof pattern, "%s", topic);
  if (!isatty(STDOUT_FILENO) || run_pager(path, at, pattern[0] ? pattern : NULL) != 0) {
    if (at == 0) { FILE *f = fopen(path, "r"); char line[4096]; if (f) { while (fgets(line, sizeof line, f)) fputs(line, stdout); fclose(f); } }
    else print_region(path, at, chapter);
  }
}

/* Chapter and section titles, for tab completion: fills `out` with up to
   `cap` strdup'd titles, returns the count. */
int manual_titles(char **out, int cap) {
  const char *path = manual_path();
  if (!path) return 0;
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  char line[4096]; int n = 0; long ln = 0; int past = 0;
  while (fgets(line, sizeof line, f) && n < cap) {
    ln++;
    if (!past) { if (ln > 6 && is_heading(line)) past = 1; else continue; }
    if (!is_heading(line)) continue;
    const char *p = line; int dots = 0;
    while (isdigit((unsigned char)*p) || *p == '.') { if (*p == '.') dots++; p++; }
    if (dots > 0) continue;                      /* chapters only: the list stays short */
    while (*p == ' ') p++;
    char *e = strchr(p, '\n'); if (e) *e = '\0';
    char *t = xstrdup(p);
    for (char *q = t; *q; q++) *q = (char)tolower((unsigned char)*q);
    out[n++] = t;
  }
  fclose(f);
  return n;
}
