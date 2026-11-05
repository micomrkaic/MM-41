/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Workdays and holiday calendars.
 *
 * A workday is a Monday to Friday that is not in the installed holiday
 * calendar. The calendar is a sorted list of serial days kept by this
 * module; it is empty until `sethol` or `usehol` installs one, or until
 * config.txt names one (`holidays = us`), which is loaded on first use.
 *
 * A calendar comes from a file in the configuration directory,
 * holidays/<name>.txt (one date per line, "d.m.yyyy" or YYYYMMDD, anything
 * after the date is a comment, # comments and blank lines allowed), or from
 * built-in rules: "us" (the federal holidays, Saturday observed on Friday
 * and Sunday on Monday) and "si" (Slovenia's work-free days, not shifted).
 *
 * `workdays` follows Excel's NETWORKDAYS: both ends inclusive, negative when
 * the second date precedes the first. `workdayplus` follows WORKDAY: the
 * start date itself is not counted. */

#include "workdays.h"
#include "calendar.h"
#include "errors.h"
#include "globals.h"
#include "ops.h"
#include <gsl/gsl_matrix.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the installed calendar ---- */
static long *hol = NULL;
static int   nhol = 0, cap = 0;
static bool  loaded = false;          /* an explicit sethol/usehol/clrhol, or the config calendar tried */

static int cmp_long(const void *a, const void *b) {
  long x = *(const long *)a, y = *(const long *)b;
  return x < y ? -1 : x > y;
}

/* A growable list of serial days, sorted and deduplicated on demand. */
typedef struct { long *v; int n, cap; } DayList;

static int list_add(DayList *l, long s) {
  if (l->n == l->cap) {
    int c = l->cap ? l->cap * 2 : 32;
    long *v = realloc(l->v, (size_t)c * sizeof *v);
    if (!v) return mm_error("holidays: out of memory");
    l->v = v; l->cap = c;
  }
  l->v[l->n++] = s;
  return 0;
}

static void list_finish(DayList *l) {
  if (l->n < 2) return;
  qsort(l->v, (size_t)l->n, sizeof *l->v, cmp_long);
  int k = 1;
  for (int i = 1; i < l->n; i++)
    if (l->v[i] != l->v[k - 1]) l->v[k++] = l->v[i];
  l->n = k;
}

static void install(DayList *l) {
  free(hol);
  hol = l->v; nhol = l->n; cap = l->cap;
  l->v = NULL; l->n = l->cap = 0;
  loaded = true;
}

void workdays_shutdown(void) { free(hol); hol = NULL; nhol = cap = 0; loaded = false; }

static bool is_holiday(long s) {
  int lo = 0, hi = nhol - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (hol[mid] == s) return true;
    if (hol[mid] < s) lo = mid + 1; else hi = mid - 1;
  }
  return false;
}

static bool is_workday(long s) { return !cal_is_weekend(s) && !is_holiday(s); }

/* ---- built-in rules ---- */

/* the n-th (1-based; n = -1 for the last) given weekday (0 = Sunday) of a month */
static long nth_weekday(long y, int m, int dow, int n) {
  if (n > 0) {
    long first = cal_civil_to_serial(y, m, 1);
    int off = (dow - cal_dow(first) + 7) % 7;
    return first + off + 7L * (n - 1);
  }
  long last = cal_civil_to_serial(y, m, cal_days_in_month(y, m));
  int off = (cal_dow(last) - dow + 7) % 7;
  return last - off;
}

/* a fixed-date holiday observed on the nearest weekday */
static long observed(long y, int m, int d) {
  long s = cal_civil_to_serial(y, m, d);
  int w = cal_dow(s);
  if (w == 6) return s - 1;
  if (w == 0) return s + 1;
  return s;
}

static int rules_us(DayList *l, long y) {
  long days[] = {
    observed(y, 1, 1),               /* New Year's Day */
    nth_weekday(y, 1, 1, 3),         /* Birthday of Martin Luther King, Jr. */
    nth_weekday(y, 2, 1, 3),         /* Washington's Birthday */
    nth_weekday(y, 5, 1, -1),        /* Memorial Day */
    observed(y, 6, 19),              /* Juneteenth */
    observed(y, 7, 4),               /* Independence Day */
    nth_weekday(y, 9, 1, 1),         /* Labor Day */
    nth_weekday(y, 10, 1, 2),        /* Columbus Day */
    observed(y, 11, 11),             /* Veterans Day */
    nth_weekday(y, 11, 4, 4),        /* Thanksgiving Day */
    observed(y, 12, 25),             /* Christmas Day */
  };
  for (size_t i = 0; i < sizeof days / sizeof days[0]; i++)
    if (list_add(l, days[i]) != 0) return -1;
  /* next year's New Year's Day falling on a Saturday is observed on 31 December */
  long ny = observed(y + 1, 1, 1);
  if (ny == cal_civil_to_serial(y, 12, 31) && list_add(l, ny) != 0) return -1;
  return 0;
}

static int rules_si(DayList *l, long y) {
  static const int fixed[][2] = {
    {1, 1}, {1, 2},                  /* novo leto */
    {2, 8},                          /* Prešernov dan */
    {4, 27},                         /* dan upora proti okupatorju */
    {5, 1}, {5, 2},                  /* praznik dela */
    {6, 25},                         /* dan državnosti */
    {8, 15},                         /* Marijino vnebovzetje */
    {10, 31},                        /* dan reformacije */
    {11, 1},                         /* dan spomina na mrtve */
    {12, 25},                        /* božič */
    {12, 26},                        /* dan samostojnosti in enotnosti */
  };
  for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
    if (list_add(l, cal_civil_to_serial(y, fixed[i][0], fixed[i][1])) != 0) return -1;
  return list_add(l, cal_easter_serial(y) + 1);   /* velikonočni ponedeljek */
}

typedef int (*rulefn)(DayList *, long);
static rulefn rules_for(const char *name) {
  if (!strcmp(name, "us")) return rules_us;
  if (!strcmp(name, "si")) return rules_si;
  return NULL;
}

/* ---- files ---- */

static const char *calendar_path(const char *name) {
  if (strchr(name, '/') || strstr(name, ".txt")) return name;      /* a path as given */
  static char rel[128];
  snprintf(rel, sizeof rel, "holidays/%.63s.txt", name);
  return app_path(rel);
}

static int read_file(DayList *l, const char *path, const char *what) {
  FILE *f = fopen(path, "r");
  if (!f) return -1;                                   /* the caller decides whether that is an error */
  char line[256];
  int lineno = 0, rc = 0;
  while (fgets(line, sizeof line, f)) {
    lineno++;
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;
    char tok[64]; int n = 0;
    if (sscanf(p, "%63s%n", tok, &n) != 1) continue;
    long s;
    if (cal_parse_date_string(tok, &s) != 0) {
      rc = mm_error("%s: %s line %d: '%s' is not a date", what, path, lineno, tok);
      break;
    }
    if (list_add(l, s) != 0) { rc = -1; break; }
  }
  fclose(f);
  return rc;
}

/* The calendar `name`: the file if there is one, else the rules for the
   years around today (year - 1 .. year + 2), else an error. */
static int load_named(DayList *l, const char *name, const char *what) {
  const char *path = calendar_path(name);
  int rc = read_file(l, path, what);
  if (rc == 0) return 0;
  if (rc < 0 && l->n == 0) {
    rulefn r = rules_for(name);
    if (!r) return mm_error("%s: no holiday calendar '%s' (no holidays/%s.txt in the configuration directory, and the built-in rules are \"us\" and \"si\")", what, name, name);
    long y; int m, d;
    cal_serial_to_civil(cal_today_serial(), &y, &m, &d);
    for (long yy = y - 1; yy <= y + 2; yy++)
      if (r(l, yy) != 0) return -1;
    return 0;
  }
  return -1;
}

static int push_list(Stack *s, const DayList *l, const char *what) {
  if (l->n == 0) return mm_error("%s: the calendar is empty", what);
  gsl_matrix *m = gsl_matrix_alloc((size_t)l->n, 1);
  if (!m) return mm_error("%s: out of memory", what);
  for (int i = 0; i < l->n; i++) gsl_matrix_set(m, (size_t)i, 0, (double)cal_serial_to_ymd(l->v[i]));
  push_matrix_real(s, m);
  return 0;
}

/* ---- words ---- */

int op_holidays(Stack *s) {
  char *name;
  if (pop_string_arg(s, "holidays", &name) != 0) return -1;
  DayList l = {0};
  int rc = load_named(&l, name, "holidays");
  free(name);
  if (rc == 0) { list_finish(&l); rc = push_list(s, &l, "holidays"); }
  free(l.v);
  return rc;
}

int op_yholidays(Stack *s) {
  char *name; double y;
  if (pop_string_arg(s, "yholidays", &name) != 0) return -1;
  if (pop_real_arg(s, "yholidays", &y) != 0) { free(name); return -1; }
  if (y != floor(y) || y < 1583 || y > 9999) { free(name); return mm_error("yholidays: the year must be a whole number from 1583"); }
  rulefn r = rules_for(name);
  if (!r) { int rc = mm_error("yholidays: no built-in rules for '%s' (built in: \"us\", \"si\"); a file calendar has its own years", name); free(name); return rc; }
  free(name);
  DayList l = {0};
  int rc = r(&l, (long)y);
  if (rc == 0) { list_finish(&l); rc = push_list(s, &l, "yholidays"); }
  free(l.v);
  return rc;
}

/* H -- : install the dates of a real vector */
int op_sethol(Stack *s) {
  const stack_element *e = &s->items[s->top];
  if (e->type != TYPE_MATRIX_REAL || (e->matrix_real->size1 != 1 && e->matrix_real->size2 != 1))
    return mm_error("sethol: needs a real vector of dates YYYYMMDD (from holidays, yholidays or a literal)");
  const gsl_matrix *m = e->matrix_real;
  size_t n = m->size1 * m->size2;
  DayList l = {0};
  for (size_t i = 0; i < n; i++) {
    double x = m->size2 == 1 ? gsl_matrix_get(m, i, 0) : gsl_matrix_get(m, 0, i);
    long serial;
    if (x != floor(x) || cal_ymd_to_serial("sethol", (long)x, &serial) != 0) { free(l.v); return -1; }
    if (list_add(&l, serial) != 0) { free(l.v); return -1; }
  }
  list_finish(&l);
  install(&l);
  stack_element t = pop(s); stack_element_free(&t);
  return 0;
}

/* name -- : load, install and remember in config.txt */
int op_usehol(Stack *s) {
  char *name;
  if (pop_string_arg(s, "usehol", &name) != 0) return -1;
  DayList l = {0};
  if (load_named(&l, name, "usehol") != 0) { free(l.v); free(name); return -1; }
  list_finish(&l);
  install(&l);
  snprintf(holiday_calendar, sizeof holiday_calendar, "%s", name);
  free(name);
  return 0;
}

int op_clrhol(Stack *s) {
  (void)s;
  free(hol); hol = NULL; nhol = cap = 0; loaded = true;
  holiday_calendar[0] = '\0';
  return 0;
}

/* Load the calendar named in config.txt on first use. A calendar that
   cannot be loaded is an error on every use until `clrhol` or `usehol`
   replaces it: a silently wrong count would be worse. */
static int ensure_calendar(void) {
  if (loaded) return 0;
  if (!holiday_calendar[0]) { loaded = true; return 0; }
  DayList l = {0};
  if (load_named(&l, holiday_calendar, "holidays") != 0) {
    free(l.v);
    return mm_error("holidays: config.txt names the calendar '%s'; fix it, or clrhol, or \"us\" usehol", holiday_calendar);
  }
  list_finish(&l);
  install(&l);
  return 0;
}

int op_hol(Stack *s) {
  if (ensure_calendar() != 0) return -1;
  DayList l = { hol, nhol, cap };
  if (nhol == 0) return mm_error("hol: no holiday calendar is installed (\"us\" usehol, or H sethol)");
  return push_list(s, &l, "hol");
}

/* Weekdays in [a, b] inclusive, a <= b, closed form. */
static long weekdays_between(long a, long b) {
  long days = b - a + 1;
  long full = days / 7, rem = days % 7;
  long n = full * 5;
  int w = cal_dow(a);
  for (long i = 0; i < rem; i++) {
    int d = (w + (int)i) % 7;
    if (d != 0 && d != 6) n++;
  }
  return n;
}

/* holidays in [a, b] that fall on weekdays (the list is sorted) */
static long weekday_holidays_between(long a, long b) {
  long n = 0;
  for (int i = 0; i < nhol; i++) {
    if (hol[i] < a) continue;
    if (hol[i] > b) break;
    if (!cal_is_weekend(hol[i])) n++;
  }
  return n;
}

int op_workdays(Stack *s) {
  long a, b;
  if (!cal_elem_is_date(&s->items[s->top - 1]) || !cal_elem_is_date(&s->items[s->top]))
    return mm_error("workdays: needs two dates (\"d.m.yyyy\" or YYYYMMDD)");
  if (cal_elem_to_serial(&s->items[s->top - 1], "workdays", &a) != 0) return -1;
  if (cal_elem_to_serial(&s->items[s->top], "workdays", &b) != 0) return -1;
  if (ensure_calendar() != 0) return -1;
  long sign = 1;
  if (b < a) { long t = a; a = b; b = t; sign = -1; }
  long n = weekdays_between(a, b) - weekday_holidays_between(a, b);
  stack_element t = pop(s); stack_element_free(&t);
  t = pop(s); stack_element_free(&t);
  push_real(s, (double)(sign * n));
  return 0;
}

int op_workdayplus(Stack *s) {
  double n;
  if (!cal_elem_is_date(&s->items[s->top - 1])) return mm_error("workdayplus: needs a date and a number of workdays");
  stack_coerce_real(s, 0);
  if (s->items[s->top].type != TYPE_REAL) return mm_error("workdayplus: the number of workdays must be a number");
  n = s->items[s->top].real;
  if (n != floor(n) || fabs(n) > 1000000) return mm_error("workdayplus: the number of workdays must be a whole number");
  long d;
  if (cal_elem_to_serial(&s->items[s->top - 1], "workdayplus", &d) != 0) return -1;
  if (ensure_calendar() != 0) return -1;
  long left = (long)fabs(n), step = n < 0 ? -1 : 1;
  while (left > 0) {
    d += step;
    if (is_workday(d)) left--;
  }
  bool as_string = s->items[s->top - 1].type == TYPE_STRING;
  stack_element t = pop(s); stack_element_free(&t);
  t = pop(s); stack_element_free(&t);
  if (as_string) {
    char buf[32];
    cal_serial_to_string(d, buf, sizeof buf);
    push_string(s, buf);
  } else {
    push_real(s, (double)cal_serial_to_ymd(d));
  }
  return 0;
}

int op_isworkday(Stack *s) {
  long d;
  if (!cal_elem_is_date(&s->items[s->top])) return mm_error("isworkday: needs a date");
  if (cal_elem_to_serial(&s->items[s->top], "isworkday", &d) != 0) return -1;
  if (ensure_calendar() != 0) return -1;
  bool w = is_workday(d);
  stack_element t = pop(s); stack_element_free(&t);
  push_real(s, w ? 1.0 : 0.0);
  return 0;
}
