/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Reading numbers from a file without saying how many there are: readmat
   takes a table (one row per line, columns separated by spaces, tabs,
   commas or semicolons), readvec everything as one column, readcol one
   column of a table. Blank lines, lines starting with #, \ or //, and
   lines with a token that is not a number (a header) are skipped. */

#include "read_fun.h"
#include "errors.h"
#include "ops.h"
#include "stack.h"
#include <ctype.h>
#include <errno.h>
#include <gsl/gsl_matrix.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parse one line into numbers; returns the count, -1 if a token is not a number. */
static long parse_line(char *line, double *out, long cap) {
  long n = 0;
  for (char *p = line; *p; ) {
    while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == ';')) p++;
    if (!*p) break;
    char *end;
    double v = strtod(p, &end);
    if (end == p) return -1;
    if (*end && !isspace((unsigned char)*end) && *end != ',' && *end != ';') return -1;
    if (n < cap) out[n] = v;
    n++;
    p = end;
  }
  return n;
}

static int is_comment(const char *line) {
  while (*line == ' ' || *line == '\t') line++;
  return *line == '#' || *line == '\\' || (line[0] == '/' && line[1] == '/') || *line == '\0' || *line == '\n' || *line == '\r';
}

/* Read the table in `path`: rows x cols, cols from the first data line. */
static gsl_matrix *read_table(const char *path, const char *what, int flatten) {
  FILE *f = fopen(path, "r");
  if (!f) { mm_error("%s: cannot open %s (%s)", what, path, strerror(errno)); return NULL; }
  size_t cap = 1024, n = 0; long cols = 0, rows = 0;
  double *data = xmalloc(cap * sizeof *data);
  char line[65536];
  long lineno = 0;
  while (fgets(line, sizeof line, f)) {
    lineno++;
    if (is_comment(line)) continue;
    double row[4096];
    long k = parse_line(line, row, 4096);
    if (k < 0) {                                 /* a header or a label: skip it */
      if (rows == 0) continue;
      mm_error("%s: line %ld of %s is not numbers", what, lineno, path); free(data); fclose(f); return NULL;
    }
    if (k == 0) continue;
    if (k > 4096) { mm_error("%s: more than 4096 columns on line %ld", what, lineno); free(data); fclose(f); return NULL; }
    if (!flatten) {
      if (cols == 0) cols = k;
      else if (k != cols) { mm_error("%s: line %ld of %s has %ld numbers, the first row %ld", what, lineno, path, k, cols); free(data); fclose(f); return NULL; }
    }
    if (n + (size_t)k > cap) { while (n + (size_t)k > cap) cap *= 2; data = xrealloc(data, cap * sizeof *data); }
    memcpy(data + n, row, (size_t)k * sizeof *row);
    n += (size_t)k; rows++;
  }
  fclose(f);
  if (n == 0) { mm_error("%s: no numbers in %s", what, path); free(data); return NULL; }
  gsl_matrix *m = flatten ? gsl_matrix_alloc(n, 1) : gsl_matrix_alloc((size_t)rows, (size_t)cols);
  for (size_t i = 0; i < n; i++) m->data[i] = data[i];
  free(data);
  return m;
}

static int pop_path(Stack *s, const char *what, char *path, size_t cap) {
  if (s->items[s->top].type != TYPE_STRING) return mm_error("%s: needs a file name as a string", what);
  snprintf(path, cap, "%s", s->items[s->top].string);
  stack_element e = pop(s); stack_element_free(&e);
  return 0;
}

/* "file" -- M */
int op_readmat(Stack *s) {
  char path[4096];
  if (pop_path(s, "readmat", path, sizeof path) != 0) return -1;
  gsl_matrix *m = read_table(path, "readmat", 0);
  if (!m) return -1;
  push_matrix_real(s, m);
  return 0;
}

/* "file" -- v: every number in the file as a column */
int op_readvec(Stack *s) {
  char path[4096];
  if (pop_path(s, "readvec", path, sizeof path) != 0) return -1;
  gsl_matrix *m = read_table(path, "readvec", 1);
  if (!m) return -1;
  push_matrix_real(s, m);
  return 0;
}

/* "file" j -- v: column j (0-based) of the table */
int op_readcol(Stack *s) {
  double j;
  if (pop_real_arg(s, "readcol", &j) != 0) return -1;
  char path[4096];
  if (pop_path(s, "readcol", path, sizeof path) != 0) return -1;
  gsl_matrix *m = read_table(path, "readcol", 0);
  if (!m) return -1;
  if (!(j >= 0 && j < (double)m->size2) || j != floor(j)) { size_t cols = m->size2; gsl_matrix_free(m); return mm_error("readcol: the table has %zu columns, numbered from 0", cols); }
  gsl_matrix *v = gsl_matrix_alloc(m->size1, 1);
  for (size_t i = 0; i < m->size1; i++) gsl_matrix_set(v, i, 0, gsl_matrix_get(m, i, (size_t)j));
  gsl_matrix_free(m);
  push_matrix_real(s, v);
  return 0;
}

/* A j -- v (column, as a column vector); A i -- v (row, as a row vector) */
static int take_line(Stack *s, const char *what, int column) {
  double k;
  if (pop_real_arg(s, what, &k) != 0) return -1;
  stack_element *e = &s->items[s->top];
  if (e->type != TYPE_MATRIX_REAL) return mm_error("%s: needs a real matrix", what);
  gsl_matrix *m = e->matrix_real;
  size_t limit = column ? m->size2 : m->size1;
  if (!(k >= 0 && k < (double)limit) || k != floor(k)) return mm_error("%s: the matrix has %zu %s, numbered from 0", what, limit, column ? "columns" : "rows");
  gsl_matrix *v = column ? gsl_matrix_alloc(m->size1, 1) : gsl_matrix_alloc(1, m->size2);
  if (column) for (size_t i = 0; i < m->size1; i++) gsl_matrix_set(v, i, 0, gsl_matrix_get(m, i, (size_t)k));
  else for (size_t j = 0; j < m->size2; j++) gsl_matrix_set(v, 0, j, gsl_matrix_get(m, (size_t)k, j));
  stack_element old = pop(s); stack_element_free(&old);
  push_matrix_real(s, v);
  return 0;
}
int op_getcol(Stack *s) { return take_line(s, "getcol", 1); }
int op_getrow(Stack *s) { return take_line(s, "getrow", 0); }
