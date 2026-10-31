/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The finance module: time value of money, cash flows, amortisation, rate
 * conversions, depreciation, bonds and day counts. Every word computes in
 * decimals and leaves a decimal, whatever it was given: a real argument is
 * converted on the way in, so `12 n 1 i% 10000 pv 0 fv ?pmt` works in either
 * number mode. Cash flows are vectors in time order (an N×2 matrix gives
 * amounts and repeat counts; for the dated words, amounts and dates). Dates
 * are the strings of the dates module ("d.m.yyyy") or integers YYYYMMDD.
 * Rates are always per period, in percent. Cash received is +, cash paid -. */

#include "module.h"
#include "errors.h"
#include "stack.h"
#include "globals.h"
#include "decimal.h"
#include "fin.h"
#include <gsl/gsl_matrix.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- argument helpers: peek, then pop, so an error leaves the stack alone ---- */

static int top_is_number(const Stack *stack, int depth) {
  value_type t = stack->items[stack->top - depth].type;
  return t == TYPE_REAL || t == TYPE_DECIMAL;
}

static void elem_to_dec(const stack_element *e, mm_dec *out) {
  if (e->type == TYPE_DECIMAL) *out = e->dec;
  else dec_from_double(out, e->real);
}

/* Pop k numbers (reals or decimals) into out[0..k-1], out[0] being the deepest. */
static int pop_decs(Stack *stack, const char *what, int k, mm_dec *out) {
  for (int d = 0; d < k; d++)
    if (!top_is_number(stack, d))
      return k == 1 ? mm_error("%s: needs a number (real or decimal)", what)
                    : mm_error("%s: needs %d numbers (reals or decimals)", what, k);
  for (int d = k - 1; d >= 0; d--) {
    stack_element e = pop(stack);
    elem_to_dec(&e, &out[d]);
  }
  return 0;
}

static int dec_to_whole(const mm_dec *d, const char *what, const char *which, long *out) {
  bool ok;
  long v = dec_to_long(d, &ok);
  if (!ok) return mm_error("%s: %s must be a whole number", what, which);
  *out = v;
  return 0;
}

/* A date argument: the string "d.m.yyyy" or the number YYYYMMDD. */
static int elem_to_serial(const stack_element *e, const char *what, long *serial) {
  if (e->type == TYPE_STRING) {
    int d, m; long y; int n = 0;
    if (sscanf(e->string, " %d.%d.%ld %n", &d, &m, &y, &n) != 3 || e->string[n] != '\0')
      return mm_error("%s: a date is \"d.m.yyyy\" or the number YYYYMMDD, not \"%s\"", what, e->string);
    return fin_dmy_to_serial(what, d, m, y, serial);
  }
  if (e->type == TYPE_REAL || e->type == TYPE_DECIMAL) {
    mm_dec d; long ymd;
    elem_to_dec(e, &d);
    if (dec_to_whole(&d, what, "a numeric date (YYYYMMDD)", &ymd) != 0) return -1;
    return fin_ymd_to_serial(what, ymd, serial);
  }
  return mm_error("%s: a date is \"d.m.yyyy\" or the number YYYYMMDD", what);
}

static int top_is_date(const Stack *stack, int depth) {
  value_type t = stack->items[stack->top - depth].type;
  return t == TYPE_REAL || t == TYPE_DECIMAL || t == TYPE_STRING;
}

static void push_dec_result(Stack *stack, const mm_dec *r) { push_decimal(stack, r); }

static int fixed_str(const mm_dec *d, char *buf, size_t n) {
  dec_to_fixed(d, print_precision, buf, n);
  return 0;
}

/* ---- TVM registers ---- */

static int store_reg(Stack *stack, const char *what, mm_dec *reg) {
  mm_dec v;
  if (pop_decs(stack, what, 1, &v) != 0) return -1;
  *reg = v;
  return 0;
}
static int op_set_n(Stack *s)   { return store_reg(s, "n", &T_n); }
static int op_set_i(Stack *s)   { return store_reg(s, "i%", &T_i); }
static int op_set_pv(Stack *s)  { return store_reg(s, "pv", &T_pv); }
static int op_set_pmt(Stack *s) { return store_reg(s, "pmt", &T_pmt); }
static int op_set_fv(Stack *s)  { return store_reg(s, "fv", &T_fv); }
static int op_beg(Stack *s)     { (void)s; T_begin = 1; return 0; }
static int op_end(Stack *s)     { (void)s; T_begin = 0; return 0; }
static int op_clrtvm(Stack *s)  { (void)s; fin_tvm_reset(); return 0; }

static int op_solve_n(Stack *s) {
  mm_dec r;
  if (fin_tvm_n(&T_i, &T_pv, &T_pmt, &T_fv, T_begin, &r) != 0) return -1;
  T_n = r; push_dec_result(s, &r); return 0;
}
static int op_solve_i(Stack *s) {
  mm_dec r;
  if (fin_tvm_i(&T_n, &T_pv, &T_pmt, &T_fv, T_begin, &r) != 0) return -1;
  T_i = r; push_dec_result(s, &r); return 0;
}
static int op_solve_pv(Stack *s) {
  mm_dec r;
  if (fin_tvm_pv(&T_n, &T_i, &T_pmt, &T_fv, T_begin, &r) != 0) return -1;
  T_pv = r; push_dec_result(s, &r); return 0;
}
static int op_solve_pmt(Stack *s) {
  mm_dec r;
  if (fin_tvm_pmt(&T_n, &T_i, &T_pv, &T_fv, T_begin, &r) != 0) return -1;
  T_pmt = r; push_dec_result(s, &r); return 0;
}
static int op_solve_fv(Stack *s) {
  mm_dec r;
  if (fin_tvm_fv(&T_n, &T_i, &T_pv, &T_pmt, T_begin, &r) != 0) return -1;
  T_fv = r; push_dec_result(s, &r); return 0;
}

static int op_tvm(Stack *s) {
  (void)s;
  char b[64];
  fixed_str(&T_n, b, sizeof b);   printf("  n    = %s\n", b);
  fixed_str(&T_i, b, sizeof b);   printf("  i%%   = %s per period\n", b);
  fixed_str(&T_pv, b, sizeof b);  printf("  pv   = %s\n", b);
  fixed_str(&T_pmt, b, sizeof b); printf("  pmt  = %s\n", b);
  fixed_str(&T_fv, b, sizeof b);  printf("  fv   = %s\n", b);
  printf("  mode = %s\n", T_begin ? "beg (payments at the start of the period)" : "end (payments at the end of the period)");
  return 0;
}

/* ---- amortisation ---- */

static int op_amort(Stack *s) {
  (void)s;
  AmortRow *rows; long n; mm_dec totI, totP;
  if (fin_amort_rows(&rows, &n, &totI, &totP) != 0) return -1;
  char b1[64], b2[64], b3[64], b4[64];
  printf("  per        payment       interest      principal        balance\n");
  printf("  --- -------------- -------------- -------------- --------------\n");
  for (long p = 0; p < n; p++) {
    fixed_str(&rows[p].payment, b1, sizeof b1);  fixed_str(&rows[p].interest, b2, sizeof b2);
    fixed_str(&rows[p].principal, b3, sizeof b3); fixed_str(&rows[p].balance, b4, sizeof b4);
    printf("  %3ld %14s %14s %14s %14s\n", p + 1, b1, b2, b3, b4);
  }
  fixed_str(&totI, b2, sizeof b2); fixed_str(&totP, b3, sizeof b3);
  printf("  --- -------------- -------------- -------------- --------------\n");
  printf("  tot                %14s %14s\n", b2, b3);
  free(rows);
  return 0;
}

/* -- M : the schedule as an N×4 real matrix (payment interest principal balance) */
static int op_amortm(Stack *s) {
  AmortRow *rows; long n; mm_dec totI, totP;
  if (fin_amort_rows(&rows, &n, &totI, &totP) != 0) return -1;
  gsl_matrix *m = gsl_matrix_alloc((size_t)n, 4);
  for (long p = 0; p < n; p++) {
    gsl_matrix_set(m, (size_t)p, 0, dec_to_double(&rows[p].payment));
    gsl_matrix_set(m, (size_t)p, 1, dec_to_double(&rows[p].interest));
    gsl_matrix_set(m, (size_t)p, 2, dec_to_double(&rows[p].principal));
    gsl_matrix_set(m, (size_t)p, 3, dec_to_double(&rows[p].balance));
  }
  free(rows);
  push_matrix_real(s, m);
  return 0;
}

/* ---- cash flows as matrices ---- */

/* The matrix under the top k items must be real: a vector (amounts, one
   period each) or N×2 (amount, repeat count). */
static int cash_flows_at(const Stack *stack, int depth, const char *what, CashFlow **out, int *n) {
  const stack_element *e = &stack->items[stack->top - depth];
  if (e->type != TYPE_MATRIX_REAL)
    return mm_error("%s: cash flows are a real vector in time order, or an N×2 matrix of amount and count", what);
  const gsl_matrix *m = e->matrix_real;
  size_t rows = m->size1, cols = m->size2;
  int with_counts = 0;
  size_t N;
  if (cols == 1) N = rows;
  else if (rows == 1) N = cols;
  else if (cols == 2) { N = rows; with_counts = 1; }
  else return mm_error("%s: cash flows are a vector or an N×2 matrix, not %zu×%zu", what, rows, cols);
  CashFlow *cf = malloc(N * sizeof *cf);
  if (!cf) return mm_error("%s: out of memory", what);
  long total = 0;
  for (size_t j = 0; j < N; j++) {
    double amt = with_counts ? gsl_matrix_get(m, j, 0) : (cols == 1 ? gsl_matrix_get(m, j, 0) : gsl_matrix_get(m, 0, j));
    double cnt = with_counts ? gsl_matrix_get(m, j, 1) : 1.0;
    if (!isfinite(amt)) { free(cf); return mm_error("%s: cash flow %zu is not finite", what, j + 1); }
    if (cnt != floor(cnt) || cnt < 1 || cnt > 100000) { free(cf); return mm_error("%s: count %zu must be a whole number >= 1", what, j + 1); }
    dec_from_double(&cf[j].amt, amt);
    cf[j].count = (long)cnt;
    total += cf[j].count;
  }
  if (total > 100000) { free(cf); return mm_error("%s: too many periods", what); }
  *out = cf; *n = (int)N;
  return 0;
}

/* cf rate -- x */
static int op_npv(Stack *s) {
  if (!top_is_number(s, 0)) return mm_error("npv: the rate (percent per period) must be a number");
  CashFlow *cf; int n;
  if (cash_flows_at(s, 1, "npv", &cf, &n) != 0) return -1;
  mm_dec rate, r;
  pop_decs(s, "npv", 1, &rate);
  stack_element m = pop(s); stack_element_free(&m);
  int rc = fin_npv(cf, n, &rate, &r);
  free(cf);
  if (rc != 0) return -1;
  push_dec_result(s, &r);
  return 0;
}

/* cf -- x */
static int op_irr(Stack *s) {
  CashFlow *cf; int n;
  if (cash_flows_at(s, 0, "irr", &cf, &n) != 0) return -1;
  stack_element m = pop(s); stack_element_free(&m);
  mm_dec r;
  int rc = fin_irr(cf, n, &r);
  free(cf);
  if (rc != 0) return -1;
  push_dec_result(s, &r);
  return 0;
}

/* Dated flows: an N×2 real matrix of amount and date YYYYMMDD. */
static int dated_flows_at(const Stack *stack, int depth, const char *what, DatedFlow **out, int *n) {
  const stack_element *e = &stack->items[stack->top - depth];
  if (e->type != TYPE_MATRIX_REAL || e->matrix_real->size2 != 2 || e->matrix_real->size1 < 1)
    return mm_error("%s: dated cash flows are an N×2 real matrix of amount and date YYYYMMDD", what);
  const gsl_matrix *m = e->matrix_real;
  size_t N = m->size1;
  DatedFlow *cf = malloc(N * sizeof *cf);
  if (!cf) return mm_error("%s: out of memory", what);
  for (size_t j = 0; j < N; j++) {
    double amt = gsl_matrix_get(m, j, 0), dt = gsl_matrix_get(m, j, 1);
    if (!isfinite(amt)) { free(cf); return mm_error("%s: amount %zu is not finite", what, j + 1); }
    if (dt != floor(dt) || dt < 10101 || dt > 99991231) { free(cf); return mm_error("%s: date %zu must be a whole number YYYYMMDD", what, j + 1); }
    if (fin_ymd_to_serial(what, (long)dt, &cf[j].serial) != 0) { free(cf); return -1; }
    dec_from_double(&cf[j].amt, amt);
  }
  *out = cf; *n = (int)N;
  return 0;
}

/* M rate -- x */
static int op_xnpv(Stack *s) {
  if (!top_is_number(s, 0)) return mm_error("xnpv: the annual rate (percent) must be a number");
  DatedFlow *cf; int n;
  if (dated_flows_at(s, 1, "xnpv", &cf, &n) != 0) return -1;
  mm_dec rate, r;
  pop_decs(s, "xnpv", 1, &rate);
  stack_element m = pop(s); stack_element_free(&m);
  int rc = fin_xnpv(cf, n, &rate, &r);
  free(cf);
  if (rc != 0) return -1;
  push_dec_result(s, &r);
  return 0;
}

/* M -- x */
static int op_xirr(Stack *s) {
  DatedFlow *cf; int n;
  if (dated_flows_at(s, 0, "xirr", &cf, &n) != 0) return -1;
  stack_element m = pop(s); stack_element_free(&m);
  mm_dec r;
  int rc = fin_xirr(cf, n, &r);
  free(cf);
  if (rc != 0) return -1;
  push_dec_result(s, &r);
  return 0;
}

/* ---- rates, depreciation, bonds: k numbers in, one decimal out ---- */

#define NUMERIC_OP(fname, what, k, call)                      \
  static int fname(Stack *s) {                                \
    mm_dec a[k], r;                                           \
    if (pop_decs(s, what, k, a) != 0) return -1;              \
    if ((call) != 0) return -1;                               \
    push_dec_result(s, &r);                                   \
    return 0;                                                 \
  }

NUMERIC_OP(op_nom2eff, "nom>eff", 2, fin_nom2eff(&a[0], &a[1], &r))
NUMERIC_OP(op_eff2nom, "eff>nom", 2, fin_eff2nom(&a[0], &a[1], &r))
NUMERIC_OP(op_sl,      "sl",      3, fin_sl(&a[0], &a[1], &a[2], &r))
NUMERIC_OP(op_soyd,    "soyd",    4, fin_soyd(&a[0], &a[1], &a[2], &a[3], &r))
NUMERIC_OP(op_db,      "db",      5, fin_db(&a[0], &a[1], &a[2], &a[3], &a[4], &r))
NUMERIC_OP(op_bprice,  "bprice",  4, fin_bond_price(&a[0], &a[1], &a[2], &a[3], &r))
NUMERIC_OP(op_byield,  "byield",  4, fin_bond_yield(&a[0], &a[1], &a[2], &a[3], &r))

/* ---- dated bonds and day counts ---- */

/* Pop a dated-bond argument list: settle maturity, k numbers, freq basis.
   Everything is checked before anything is popped. */
static int pop_dated(Stack *s, const char *what, int k, long *settle, long *maturity, mm_dec *nums, int *freq, int *basis) {
  int total = k + 4;
  for (int d = 0; d < k + 2; d++)
    if (!top_is_number(s, d)) return mm_error("%s: needs settle maturity, %d numbers, freq and basis", what, k);
  if (!top_is_date(s, total - 1) || !top_is_date(s, total - 2))
    return mm_error("%s: settlement and maturity are dates (\"d.m.yyyy\" or YYYYMMDD)", what);
  if (elem_to_serial(&s->items[s->top - (total - 1)], what, settle) != 0) return -1;
  if (elem_to_serial(&s->items[s->top - (total - 2)], what, maturity) != 0) return -1;
  mm_dec f, b; long fl, bl;
  elem_to_dec(&s->items[s->top - 1], &f);
  elem_to_dec(&s->items[s->top], &b);
  if (dec_to_whole(&f, what, "the coupon frequency", &fl) != 0) return -1;
  if (dec_to_whole(&b, what, "the basis", &bl) != 0) return -1;
  if (fl <= 0 || 12 % fl != 0) return mm_error("%s: coupon frequency must divide 12 (1, 2, 3, 4, 6, 12)", what);
  if (bl < 0 || bl > 4) return mm_error("%s: basis must be 0..4 (30/360, act/act, act/360, act/365, 30E/360)", what);
  pop(s); pop(s);                                            /* freq, basis: numbers own no memory */
  pop_decs(s, what, k, nums);
  stack_element e = pop(s); stack_element_free(&e);
  e = pop(s); stack_element_free(&e);
  *freq = (int)fl; *basis = (int)bl;
  return 0;
}

/* settle maturity cpn% yld% redeem freq basis -- price */
static int op_xprice(Stack *s) {
  long settle, maturity; mm_dec a[3], r; int freq, basis;
  if (pop_dated(s, "xprice", 3, &settle, &maturity, a, &freq, &basis) != 0) return -1;
  if (fin_xbond_price(settle, maturity, &a[0], &a[1], &a[2], freq, basis, &r) != 0) return -1;
  push_dec_result(s, &r);
  return 0;
}

/* settle maturity cpn% price redeem freq basis -- yld% */
static int op_xyield(Stack *s) {
  long settle, maturity; mm_dec a[3], r; int freq, basis;
  if (pop_dated(s, "xyield", 3, &settle, &maturity, a, &freq, &basis) != 0) return -1;
  if (fin_xbond_yield(settle, maturity, &a[0], &a[1], &a[2], freq, basis, &r) != 0) return -1;
  push_dec_result(s, &r);
  return 0;
}

/* settle maturity cpn% freq basis -- accrued */
static int op_xaccr(Stack *s) {
  long settle, maturity; mm_dec a[1], r; int freq, basis;
  if (pop_dated(s, "xaccr", 1, &settle, &maturity, a, &freq, &basis) != 0) return -1;
  if (fin_xaccrued(settle, maturity, &a[0], freq, basis, &r) != 0) return -1;
  push_dec_result(s, &r);
  return 0;
}

/* d1 d2 basis -- f */
static int op_yearfrac(Stack *s) {
  if (!top_is_number(s, 0)) return mm_error("yearfrac: the basis is a number 0..4");
  if (!top_is_date(s, 1) || !top_is_date(s, 2)) return mm_error("yearfrac: needs two dates and a basis");
  long s1, s2, basis;
  if (elem_to_serial(&s->items[s->top - 2], "yearfrac", &s1) != 0) return -1;
  if (elem_to_serial(&s->items[s->top - 1], "yearfrac", &s2) != 0) return -1;
  mm_dec b, r;
  elem_to_dec(&s->items[s->top], &b);
  if (dec_to_whole(&b, "yearfrac", "the basis", &basis) != 0) return -1;
  if (basis < 0 || basis > 4) return mm_error("yearfrac: basis must be 0..4 (30/360, act/act, act/360, act/365, 30E/360)");
  if (fin_year_frac(s1, s2, (int)basis, &r) != 0) return -1;
  for (int i = 0; i < 3; i++) { stack_element e = pop(s); stack_element_free(&e); }
  push_dec_result(s, &r);
  return 0;
}

/* date -- yyyymmdd */
static int op_date_to_ymd(Stack *s) {
  if (!top_is_date(s, 0)) return mm_error("date>ymd: needs a date");
  long serial;
  if (elem_to_serial(&s->items[s->top], "date>ymd", &serial) != 0) return -1;
  stack_element e = pop(s); stack_element_free(&e);
  push_real(s, (double)fin_serial_to_ymd(serial));
  return 0;
}

/* yyyymmdd -- "d.m.yyyy" */
static int op_ymd_to_date(Stack *s) {
  if (!top_is_date(s, 0)) return mm_error("ymd>date: needs a date");
  long serial;
  if (elem_to_serial(&s->items[s->top], "ymd>date", &serial) != 0) return -1;
  int d, m; long y;
  fin_serial_to_dmy(serial, &d, &m, &y);
  char buf[32];
  snprintf(buf, sizeof buf, "%02d.%02d.%ld", d, m, y);
  stack_element e = pop(s); stack_element_free(&e);
  push_string(s, buf);
  return 0;
}

static int init(void) { fin_tvm_reset(); return 0; }

static const Op table[] = {
  { "n",       1, op_set_n,     "Finance", "x --", "store the number of periods in the TVM register n", "12 n" },
  { "i%",      1, op_set_i,     "Finance", "x --", "store the interest rate per period, in percent, in i%", "1 i%" },
  { "pv",      1, op_set_pv,    "Finance", "x --", "store the present value (cash received +, paid -)", "10000 pv" },
  { "pmt",     1, op_set_pmt,   "Finance", "x --", "store the periodic payment", "-888.49 pmt" },
  { "fv",      1, op_set_fv,    "Finance", "x --", "store the future value", "0 fv" },
  { "beg",     0, op_beg,       "Finance", "--", "payments fall at the beginning of each period (annuity due)", "beg" },
  { "end",     0, op_end,       "Finance", "--", "payments fall at the end of each period (the default)", "end" },
  { "clrtvm",  0, op_clrtvm,    "Finance", "--", "clear the five TVM registers and select `end`", "clrtvm" },
  { "tvm",     0, op_tvm,       "Finance", "--", "show the TVM registers", "tvm" },
  { "?n",      0, op_solve_n,   "Finance", "-- n", "solve for the number of periods; the result is stored and pushed (a decimal)", "clrtvm 1 i% 10000 pv -888.49 pmt 0 fv ?n" },
  { "?i%",     0, op_solve_i,   "Finance", "-- i", "solve for the rate per period in percent (bracketing scan, then bisection)", "clrtvm 12 n 10000 pv -888.49 pmt 0 fv ?i%" },
  { "?pv",     0, op_solve_pv,  "Finance", "-- pv", "solve for the present value", "clrtvm 12 n 1 i% -888.49 pmt 0 fv ?pv" },
  { "?pmt",    0, op_solve_pmt, "Finance", "-- pmt", "solve for the periodic payment", "clrtvm 12 n 1 i% 10000 pv 0 fv ?pmt" },
  { "?fv",     0, op_solve_fv,  "Finance", "-- fv", "solve for the future value", "clrtvm 12 n 1 i% 0 pv -100 pmt ?fv" },
  { "amort",   0, op_amort,     "Finance", "--", "print the amortisation schedule of the TVM registers (n periods of pmt on pv at i%)", "clrtvm 12 n 1 i% 10000 pv 0 fv ?pmt drop amort" },
  { "amortm",  0, op_amortm,    "Finance", "-- M", "the amortisation schedule as an n×4 real matrix: payment, interest, principal, balance", "clrtvm 12 n 1 i% 10000 pv 0 fv ?pmt drop amortm" },
  { "npv",     2, op_npv,       "Finance", "cf rate -- x", "net present value of cash flows (a vector in time order, first at t=0, or N×2 amount/count) at rate percent per period", "[6 1 $ -1000 300 300 300 300 300] 10 npv" },
  { "irr",     1, op_irr,       "Finance", "cf -- x", "internal rate of return in percent per period of a cash-flow vector", "[2 2 $ -1000 1 300 5] irr" },
  { "nom>eff", 2, op_nom2eff,   "Finance", "nom m -- eff", "nominal annual rate compounded m times a year to the effective annual rate (percent)", "12 12 nom>eff" },
  { "eff>nom", 2, op_eff2nom,   "Finance", "eff m -- nom", "effective annual rate to the nominal rate compounded m times a year", "12.6825 12 eff>nom" },
  { "sl",      3, op_sl,        "Finance", "cost salvage life -- d", "straight-line depreciation per period", "10000 1000 5 sl" },
  { "soyd",    4, op_soyd,      "Finance", "cost salvage life year -- d", "sum-of-the-years'-digits depreciation in the given year", "10000 1000 5 1 soyd" },
  { "db",      5, op_db,        "Finance", "cost salvage life year factor -- d", "declining-balance depreciation in the given year (factor 2 = double declining), never below salvage", "10000 1000 5 1 2 db" },
  { "bprice",  4, op_bprice,    "Finance", "cpn yld n redeem -- price", "bond price per 100 face on a coupon date: coupon and yield in percent per period, n periods, redemption value", "3 3.5 20 100 bprice" },
  { "byield",  4, op_byield,    "Finance", "price cpn n redeem -- yld", "bond yield in percent per period from the price on a coupon date", "92.89 3 20 100 byield" },
  { "xnpv",    2, op_xnpv,      "Finance", "M rate -- x", "net present value of dated cash flows (N×2 matrix: amount, date YYYYMMDD) at an annual rate in percent, actual/365 from the first date", "[5 2 $ -10000 20080101 2750 20080301 4250 20081030 3250 20090215 2750 20090401] 9 xnpv" },
  { "xirr",    1, op_xirr,      "Finance", "M -- x", "annual internal rate of return in percent of dated cash flows", "[3 2 $ -1000 20250101 600 20250701 600 20260101] xirr" },
  { "xprice",  7, op_xprice,    "Finance", "settle maturity cpn yld redeem freq basis -- price", "clean price per 100 face of a dated bond: annual coupon and yield in percent, coupons per year, day-count basis 0..4", "20080215 20171115 5.75 6.5 100 2 0 xprice" },
  { "xyield",  7, op_xyield,    "Finance", "settle maturity cpn price redeem freq basis -- yld", "annual yield in percent of a dated bond from its clean price", "20080215 20161115 5.75 95.04287 100 2 0 xyield" },
  { "xaccr",   5, op_xaccr,     "Finance", "settle maturity cpn freq basis -- accrued", "accrued interest per 100 face at settlement", "20080215 20171115 5.75 2 0 xaccr" },
  { "yearfrac", 3, op_yearfrac, "Finance", "d1 d2 basis -- f", "year fraction between two dates; basis 0 = 30/360 US, 1 = actual/actual, 2 = actual/360, 3 = actual/365, 4 = 30E/360", "\"1.1.2007\" \"1.1.2008\" 3 yearfrac" },
  { "date>ymd", 1, op_date_to_ymd, "Finance", "date -- yyyymmdd", "a date string d.m.yyyy as the integer YYYYMMDD (for cash-flow matrices)", "\"15.3.2026\" date>ymd" },
  { "ymd>date", 1, op_ymd_to_date, "Finance", "yyyymmdd -- date", "an integer YYYYMMDD as the date string d.m.yyyy of the dates module", "20260315 ymd>date" },
};

const Module mm41_mod_finance = {
  "finance", "1.0", "time value of money, cash flows, depreciation, bonds, day counts",
  table, (int)(sizeof table / sizeof table[0]), init, NULL, 0
};
