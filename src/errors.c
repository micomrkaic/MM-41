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
#include <stdlib.h>
#include "errors.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

long error_count = 0;
bool errors_quiet = false;

static int say(const char *fmt, va_list ap) {
  if (!fmt) fmt = "";          /* newer gcc otherwise warns "null format string" */
  if (errors_quiet) { error_count++; return -1; }
  fflush(stdout);
  if (strncmp(fmt, "Error", 5) != 0 && strncmp(fmt, "GSL", 3) != 0) fputs("Error: ", stderr);
  vfprintf(stderr, fmt, ap);
  size_t n = strlen(fmt);
  if (n == 0 || fmt[n - 1] != '\n') fputc('\n', stderr);
  error_count++;
  return -1;
}

int mm_error_say(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = say(fmt, ap);
  va_end(ap);
  return r;
}

/* mm_error is a macro since 2.2.1 (errors.h). The function of that name
   stays in the binary for compiled modules built against the older header. */
#undef mm_error
int mm_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int mm_error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = say(fmt, ap);
  va_end(ap);
  return r;
}

void mm_warn(const char *fmt, ...) {
  va_list ap;
  if (!fmt) fmt = "";
  fflush(stdout);
  if (strncmp(fmt, "Warning", 7) != 0) fputs("Warning: ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  size_t n = strlen(fmt);
  if (n == 0 || fmt[n - 1] != '\n') fputc('\n', stderr);
}

/* ---- allocations that must succeed ---------------------------------------- */

jmp_buf *mm_nomem_escape = NULL;

_Noreturn void mm_out_of_memory(void) {
  if (mm_nomem_escape) longjmp(*mm_nomem_escape, 1);
  fflush(stdout);
  fputs("mm41: out of memory\n", stderr);
  exit(EXIT_FAILURE);
}

static void *must(void *p) {
  if (p) return p;
  mm_out_of_memory();
}

void *xmalloc(size_t size) { return must(malloc(size ? size : 1)); }
void *xcalloc(size_t count, size_t size) { return must(calloc(count ? count : 1, size ? size : 1)); }
void *xrealloc(void *p, size_t size) { return must(realloc(p, size ? size : 1)); }
char *xstrdup(const char *s) { return must(strdup(s)); }
char *xstrndup(const char *s, size_t n) { return must(strndup(s, n)); }
