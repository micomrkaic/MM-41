/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The survival module: lifetime distributions, their survival, hazard and
 * life-expectancy functions, life tables and the actuarial present values
 * built on them, and the non-parametric estimators (Kaplan-Meier,
 * Nelson-Aalen, the log-rank test) with maximum-likelihood fitting of any
 * of the laws to censored data.
 *
 * A current law is selected once, "makeham" [a b c] setlaw, and every
 * other word then takes only the age or time. Time is in whatever unit the
 * parameters are in -- years, for a mortality law. The laws:
 *
 *   exponential  [lambda]        h = lambda
 *   weibull      [k lambda]      h = (k/lambda) (t/lambda)^(k-1)
 *   gompertz     [b c]           h = b e^(ct)
 *   makeham      [a b c]         h = a + b e^(ct)        (Gompertz-Makeham)
 *   siler        [a1 b1 a2 a3 b3] h = a1 e^(-b1 t) + a2 + a3 e^(b3 t)
 *   loglogistic  [alpha beta]    S = 1 / (1 + (t/alpha)^beta)
 *   lognormal    [mu sigma]      ln T ~ N(mu, sigma)
 *   gamma        [k theta]       shape k, scale theta
 *
 * S(t) and h(t) are closed forms for each; f = h S, H = -ln S, F = 1 - S.
 * Quantiles are found by Brent's method on S, the expectations by
 * adaptive quadrature. */

#include "module.h"
#include "errors.h"
#include "globals.h"
#include "ops.h"
#include "stack.h"
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_integration.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_min.h>
#include <gsl/gsl_multimin.h>
#include <gsl/gsl_roots.h>
#include <gsl/gsl_sf_gamma.h>
#include <gsl/gsl_sort.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* the laws */

enum { L_EXP, L_WEIBULL, L_GOMPERTZ, L_MAKEHAM, L_SILER, L_LOGLOGISTIC, L_LOGNORMAL, L_GAMMA, L_COUNT };

typedef struct { const char *name; int npar; const char *pars; const char *hazard; } LawInfo;
static const LawInfo laws[L_COUNT] = {
  { "exponential", 1, "[lambda]",          "h = lambda" },
  { "weibull",     2, "[k lambda]",        "h = (k/lambda) (t/lambda)^(k-1)" },
  { "gompertz",    2, "[b c]",             "h = b e^(ct)" },
  { "makeham",     3, "[a b c]",           "h = a + b e^(ct)" },
  { "siler",       5, "[a1 b1 a2 a3 b3]",  "h = a1 e^(-b1 t) + a2 + a3 e^(b3 t)" },
  { "loglogistic", 2, "[alpha beta]",      "S = 1/(1 + (t/alpha)^beta)" },
  { "lognormal",   2, "[mu sigma]",        "ln T ~ N(mu, sigma)" },
  { "gamma",       2, "[k theta]",         "shape k, scale theta" },
};

static int    law = -1;
static double par[5];

static int law_from_name(const char *name) {
  for (int i = 0; i < L_COUNT; i++) if (strcmp(name, laws[i].name) == 0) return i;
  if (!strcmp(name, "gompertz-makeham") || !strcmp(name, "gm")) return L_MAKEHAM;
  if (!strcmp(name, "exp")) return L_EXP;
  if (!strcmp(name, "fisk")) return L_LOGLOGISTIC;
  return -1;
}

/* survival S(t) and hazard h(t) under law `l` with parameters p */
static double S_of(int l, const double *p, double t) {
  if (t <= 0) return 1.0;
  switch (l) {
  case L_EXP:         return exp(-p[0] * t);
  case L_WEIBULL:     return exp(-pow(t / p[1], p[0]));
  case L_GOMPERTZ:    return exp(-(p[0] / p[1]) * expm1(p[1] * t));
  case L_MAKEHAM:     return exp(-p[0] * t - (p[1] / p[2]) * expm1(p[2] * t));
  case L_SILER:       return exp(-(p[0] / p[1]) * (1 - exp(-p[1] * t)) - p[2] * t - (p[3] / p[4]) * expm1(p[4] * t));
  case L_LOGLOGISTIC: return 1.0 / (1.0 + pow(t / p[0], p[1]));
  case L_LOGNORMAL:   return gsl_cdf_lognormal_Q(t, p[0], p[1]);
  case L_GAMMA:       return gsl_cdf_gamma_Q(t, p[0], p[1]);
  }
  return NAN;
}

static double h_of(int l, const double *p, double t) {
  if (t < 0) return 0.0;
  switch (l) {
  case L_EXP:         return p[0];
  case L_WEIBULL:     return (p[0] / p[1]) * pow(t / p[1], p[0] - 1);
  case L_GOMPERTZ:    return p[0] * exp(p[1] * t);
  case L_MAKEHAM:     return p[0] + p[1] * exp(p[2] * t);
  case L_SILER:       return p[0] * exp(-p[1] * t) + p[2] + p[3] * exp(p[4] * t);
  case L_LOGLOGISTIC: { double u = pow(t / p[0], p[1]); return (p[1] / p[0]) * pow(t / p[0], p[1] - 1) / (1 + u); }
  case L_LOGNORMAL:   { double S = gsl_cdf_lognormal_Q(t, p[0], p[1]); double f = t > 0 ? exp(-0.5 * pow((log(t) - p[0]) / p[1], 2)) / (t * p[1] * sqrt(2 * M_PI)) : 0; return S > 0 ? f / S : INFINITY; }
  case L_GAMMA:       { double S = gsl_cdf_gamma_Q(t, p[0], p[1]); double f = t > 0 ? pow(t, p[0] - 1) * exp(-t / p[1]) / (gsl_sf_gamma(p[0]) * pow(p[1], p[0])) : (p[0] == 1 ? 1 / p[1] : 0); return S > 0 ? f / S : INFINITY; }
  }
  return NAN;
}

static int params_ok(int l, const double *p) {
  if (l == L_LOGNORMAL) return isfinite(p[0]) && isfinite(p[1]) && p[1] > 0;   /* mu may be anything */
  for (int i = 0; i < laws[l].npar; i++) if (!isfinite(p[i]) || p[i] <= 0) return 0;
  return 1;
}

static int need_law(const char *what) {
  if (law < 0) return mm_error("%s: no law selected; \"gompertz\" [b c] setlaw first (help survival)", what);
  return 0;
}

#define S(t) S_of(law, par, (t))
#define H(t) h_of(law, par, (t))

/* ------------------------------------------------------------------ */
/* numerics on the current law */

static double surv_integrand(double u, void *q) { (void)q; return S(u); }
static double tsurv_integrand(double u, void *q) { (void)q; return u * S(u); }
static double neg_pdf(double u, void *q) { (void)q; return -H(u) * S(u); }

/* integral of f from a to infinity */
static int integrate_inf(double (*f)(double, void *), double a, double *out, const char *what) {
  gsl_integration_workspace *w = gsl_integration_workspace_alloc(2000);
  gsl_function F = { f, NULL };
  double r, err;
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = gsl_integration_qagiu(&F, a, 0, 1e-9, 2000, w, &r, &err);
  gsl_set_error_handler(old);
  gsl_integration_workspace_free(w);
  num_evals = 0;
  if (st != GSL_SUCCESS && st != GSL_EROUND) return mm_error("%s: the integral did not converge (%s)", what, gsl_strerror(st));
  *out = r;
  return 0;
}

/* t with S(t) = 1 - p */
static double qroot(double t, void *q) { return S(t) - *(double *)q; }
static int quantile(double p, double *out, const char *what) {
  if (!(p > 0 && p < 1)) return mm_error("%s: p must be strictly between 0 and 1", what);
  double target = 1 - p, hi = 1;
  while (S(hi) > target && hi < 1e12) hi *= 2;
  if (S(hi) > target) return mm_error("%s: no finite quantile", what);
  gsl_function F = { qroot, &target };
  gsl_root_fsolver *s = gsl_root_fsolver_alloc(gsl_root_fsolver_brent);
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = gsl_root_fsolver_set(s, &F, 0, hi);
  double r = hi;
  for (int it = 0; st == GSL_SUCCESS && it < 200; it++) {
    st = gsl_root_fsolver_iterate(s);
    r = gsl_root_fsolver_root(s);
    if (gsl_root_test_interval(gsl_root_fsolver_x_lower(s), gsl_root_fsolver_x_upper(s), 1e-12, 1e-12) == GSL_SUCCESS) break;
  }
  gsl_set_error_handler(old);
  gsl_root_fsolver_free(s);
  if (st != GSL_SUCCESS) return mm_error("%s: the root search failed", what);
  *out = r;
  return 0;
}

/* e(t): expected remaining life at t */
static int elife_at(double t, double *out, const char *what) {
  double st = S(t);
  if (st <= 0) return mm_error("%s: S(t) is zero at t = %g", what, t);
  double integ;
  if (integrate_inf(surv_integrand, t, &integ, what) != 0) return -1;
  *out = integ / st;
  return 0;
}

/* ------------------------------------------------------------------ */
/* stack helpers */

static int is_vector(const stack_element *e) {
  return e->type == TYPE_MATRIX_REAL && (e->matrix_real->size1 == 1 || e->matrix_real->size2 == 1);
}

static int vector_at(const Stack *s, int depth, const char *what, double **out, size_t *n) {
  const stack_element *e = &s->items[s->top - depth];
  if (!is_vector(e)) return mm_error("%s: needs a real vector (an N x 1 or 1 x N matrix)", what);
  const gsl_matrix *m = e->matrix_real;
  size_t N = m->size1 * m->size2;
  double *v = malloc(N * sizeof *v);
  if (!v) return mm_error("%s: out of memory", what);
  for (size_t i = 0; i < N; i++) v[i] = m->size2 == 1 ? gsl_matrix_get(m, i, 0) : gsl_matrix_get(m, 0, i);
  *out = v; *n = N;
  return 0;
}

static void drop_n(Stack *s, int n) {
  while (n-- > 0) { stack_element e = pop(s); stack_element_free(&e); }
}

/* Apply f(t) to the top: a real, or elementwise to a real matrix. */
typedef int (*tfn)(double t, double *out, const char *what);
static int pointwise(Stack *s, const char *what, tfn f) {
  if (need_law(what) != 0) return -1;
  stack_element *e = &s->items[s->top];
  if (e->type == TYPE_MATRIX_REAL) {
    gsl_matrix *m = e->matrix_real;
    for (size_t i = 0; i < m->size1; i++)
      for (size_t j = 0; j < m->size2; j++) {
        double r;
        if (f(gsl_matrix_get(m, i, j), &r, what) != 0) return -1;
        gsl_matrix_set(m, i, j, r);
      }
    return 0;
  }
  double t, r;
  if (pop_real_arg(s, what, &t) != 0) return -1;
  if (f(t, &r, what) != 0) return -1;
  push_real(s, r);
  return 0;
}

static int f_surv(double t, double *o, const char *w)   { (void)w; *o = S(t); return 0; }
static int f_haz(double t, double *o, const char *w)    { (void)w; *o = H(t); return 0; }
static int f_cumhaz(double t, double *o, const char *w) { (void)w; double s = S(t); *o = s > 0 ? -log(s) : INFINITY; return 0; }
static int f_pdf(double t, double *o, const char *w)    { (void)w; *o = t < 0 ? 0 : H(t) * S(t); return 0; }
static int f_cdf(double t, double *o, const char *w)    { (void)w; *o = 1 - S(t); return 0; }
static int f_inv(double p, double *o, const char *w)    { return quantile(p, o, w); }
static int f_elife(double t, double *o, const char *w)  { return elife_at(t, o, w); }
static int f_qx(double t, double *o, const char *w)     { (void)w; double s = S(t); *o = s > 0 ? 1 - S(t + 1) / s : 1; return 0; }
static int f_px(double t, double *o, const char *w)     { (void)w; double s = S(t); *o = s > 0 ? S(t + 1) / s : 0; return 0; }
static int f_lx(double t, double *o, const char *w)     { (void)w; *o = 100000.0 * S(t); return 0; }

static int op_surv(Stack *s)    { return pointwise(s, "surv", f_surv); }
static int op_haz(Stack *s)     { return pointwise(s, "haz", f_haz); }
static int op_cumhaz(Stack *s)  { return pointwise(s, "cumhaz", f_cumhaz); }
static int op_lifepdf(Stack *s) { return pointwise(s, "lifepdf", f_pdf); }
static int op_lifecdf(Stack *s) { return pointwise(s, "lifecdf", f_cdf); }
static int op_lifeinv(Stack *s) { return pointwise(s, "lifeinv", f_inv); }
static int op_elife(Stack *s)   { return pointwise(s, "elife", f_elife); }
static int op_qx(Stack *s)      { return pointwise(s, "qx", f_qx); }
static int op_px(Stack *s)      { return pointwise(s, "px", f_px); }
static int op_lx(Stack *s)      { return pointwise(s, "lx", f_lx); }

/* t n -- nPx, nQx */
static int tpx(Stack *s, const char *what, int q) {
  if (need_law(what) != 0) return -1;
  double t, n;
  if (pop_two_reals(s, what, &t, &n) != 0) return -1;
  if (n < 0) return mm_error("%s: n must be >= 0", what);
  double st = S(t);
  double p = st > 0 ? S(t + n) / st : 0;
  push_real(s, q ? 1 - p : p);
  return 0;
}
static int op_tpx(Stack *s) { return tpx(s, "tpx", 0); }
static int op_nqx(Stack *s) { return tpx(s, "nqx", 1); }

/* -- x: whole-life summaries */
static int op_lifemean(Stack *s) {
  if (need_law("lifemean") != 0) return -1;
  double m; if (integrate_inf(surv_integrand, 0, &m, "lifemean") != 0) return -1;
  push_real(s, m); return 0;
}
static int op_lifemed(Stack *s) {
  if (need_law("lifemed") != 0) return -1;
  double m; if (quantile(0.5, &m, "lifemed") != 0) return -1;
  push_real(s, m); return 0;
}
static int op_lifevar(Stack *s) {
  if (need_law("lifevar") != 0) return -1;
  double m, m2;
  if (integrate_inf(surv_integrand, 0, &m, "lifevar") != 0) return -1;
  if (integrate_inf(tsurv_integrand, 0, &m2, "lifevar") != 0) return -1;   /* E[T^2] = 2 int t S(t) dt */
  push_real(s, 2 * m2 - m * m); return 0;
}
static int op_lifemode(Stack *s) {
  if (need_law("lifemode") != 0) return -1;
  double hi; if (quantile(0.999, &hi, "lifemode") != 0) return -1;
  /* coarse grid, then Brent on -f */
  int N = 400; double best = 0, bestf = -1, step = hi / N;
  for (int i = 0; i <= N; i++) { double t = i * step, f = H(t) * S(t); if (f > bestf) { bestf = f; best = t; } }
  if (best <= 0 || best >= hi) { push_real(s, best); return 0; }     /* the mode is at an end */
  gsl_function F = { neg_pdf, NULL };
  gsl_min_fminimizer *mn = gsl_min_fminimizer_alloc(gsl_min_fminimizer_brent);
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = gsl_min_fminimizer_set(mn, &F, best, best - step, best + step);
  double m = best;
  for (int it = 0; st == GSL_SUCCESS && it < 200; it++) {
    st = gsl_min_fminimizer_iterate(mn);
    m = gsl_min_fminimizer_x_minimum(mn);
    if (gsl_min_test_interval(gsl_min_fminimizer_x_lower(mn), gsl_min_fminimizer_x_upper(mn), 1e-10, 0) == GSL_SUCCESS) break;
  }
  gsl_set_error_handler(old);
  gsl_min_fminimizer_free(mn);
  push_real(s, m); return 0;
}

/* t -- e_x: curtate expectation, sum of kPx for k >= 1 */
static int op_curtate(Stack *s) {
  if (need_law("curtate") != 0) return -1;
  double t; if (pop_real_arg(s, "curtate", &t) != 0) return -1;
  double st = S(t), e = 0;
  if (st <= 0) return mm_error("curtate: S(t) is zero at t = %g", t);
  for (int k = 1; k < 100000; k++) { double p = S(t + k) / st; e += p; if (p < 1e-14) break; }
  push_real(s, e); return 0;
}

/* ---- actuarial present values, yearly, interest i% per year ---- */

/* annuity-due: sum v^k kPx, k = 0..n-1 (n < 0: whole life); insurance: sum v^(k+1) kPx q_(x+k) */
static int apv(Stack *s, const char *what, int term, int insurance) {
  if (need_law(what) != 0) return -1;
  double x, n = -1, ipct;
  if (pop_real_arg(s, what, &ipct) != 0) return -1;
  if (term && pop_real_arg(s, what, &n) != 0) return -1;
  if (pop_real_arg(s, what, &x) != 0) return -1;
  if (term && (n < 1 || n != floor(n))) return mm_error("%s: n must be a whole number of years >= 1", what);
  if (ipct <= -100) return mm_error("%s: the rate must be above -100%%", what);
  double v = 1 / (1 + ipct / 100), sx = S(x);
  if (sx <= 0) return mm_error("%s: S(x) is zero at x = %g", what, x);
  double sum = 0, vk = 1;
  for (int k = 0; k < (term ? (int)n : 100000); k++) {
    double kpx = S(x + k) / sx;
    if (kpx < 1e-15 && !term) break;
    if (insurance) { double qxk = kpx > 0 ? 1 - S(x + k + 1) / S(x + k) : 0; sum += vk * v * kpx * qxk; }
    else sum += vk * kpx;
    vk *= v;
  }
  push_real(s, sum);
  return 0;
}
static int op_ax(Stack *s)  { return apv(s, "ax", 0, 0); }
static int op_axn(Stack *s) { return apv(s, "axn", 1, 0); }
static int op_Ax(Stack *s)  { return apv(s, "Ax", 0, 1); }
static int op_Axn(Stack *s) { return apv(s, "Axn", 1, 1); }

/* x0 x1 -- M: age, lx, dx, qx, ex (complete), from a radix of 100000 at x0 */
static int op_lifetab(Stack *s) {
  if (need_law("lifetab") != 0) return -1;
  double x0, x1;
  if (pop_two_reals(s, "lifetab", &x0, &x1) != 0) return -1;
  if (x1 < x0 || x1 - x0 > 1000) return mm_error("lifetab: needs x0 <= x1 within 1000 years");
  size_t n = (size_t)floor(x1 - x0) + 1;
  double s0 = S(x0);
  if (s0 <= 0) return mm_error("lifetab: S(x0) is zero");
  gsl_matrix *m = gsl_matrix_alloc(n, 5);
  if (!m) return mm_error("lifetab: out of memory");
  for (size_t i = 0; i < n; i++) {
    double x = x0 + (double)i, lx = 100000.0 * S(x) / s0, lx1 = 100000.0 * S(x + 1) / s0, e;
    if (lx > 0) { if (elife_at(x, &e, "lifetab") != 0) { gsl_matrix_free(m); return -1; } } else e = 0;
    gsl_matrix_set(m, i, 0, x);
    gsl_matrix_set(m, i, 1, lx);
    gsl_matrix_set(m, i, 2, lx - lx1);
    gsl_matrix_set(m, i, 3, lx > 0 ? (lx - lx1) / lx : 1);
    gsl_matrix_set(m, i, 4, e);
  }
  push_matrix_real(s, m);
  return 0;
}

/* ------------------------------------------------------------------ */
/* the law register */

/* "name" [params] setlaw */
static int op_setlaw(Stack *s) {
  const stack_element *pe = &s->items[s->top], *ne = &s->items[s->top - 1];
  if (ne->type != TYPE_STRING) return mm_error("setlaw: needs \"name\" [params]; the laws are exponential, weibull, gompertz, makeham, siler, loglogistic, lognormal, gamma");
  int l = law_from_name(ne->string);
  if (l < 0) return mm_error("setlaw: no law '%s'; the laws are exponential, weibull, gompertz, makeham, siler, loglogistic, lognormal, gamma", ne->string);
  double p[5] = { 0 };
  if (laws[l].npar == 1 && (pe->type == TYPE_REAL || pe->type == TYPE_DECIMAL)) {
    p[0] = pe->type == TYPE_REAL ? pe->real : dec_to_double(&pe->dec);
  } else {
    double *v; size_t n;
    if (vector_at(s, 0, "setlaw", &v, &n) != 0) return -1;
    if ((int)n != laws[l].npar) { free(v); return mm_error("setlaw: %s takes %d parameter%s %s", laws[l].name, laws[l].npar, laws[l].npar == 1 ? "" : "s", laws[l].pars); }
    memcpy(p, v, n * sizeof *v); free(v);
  }
  if (!params_ok(l, p)) return mm_error("setlaw: the parameters of %s must be positive (mu of lognormal excepted)", laws[l].name);
  law = l; memcpy(par, p, sizeof par);
  drop_n(s, 2);
  return 0;
}

/* -- : print the current law */
static int op_law(Stack *s) {
  (void)s;
  if (law < 0) { printf("  no law selected; \"gompertz\" [b c] setlaw, or lifefit (help survival)\n"); return 0; }
  printf("  law %s %s   %s\n  parameters:", laws[law].name, laws[law].pars, laws[law].hazard);
  for (int i = 0; i < laws[law].npar; i++) printf(" %.10g", par[i]);
  printf("\n");
  return 0;
}

/* -- [params] */
static int op_lawpar(Stack *s) {
  if (need_law("lawpar") != 0) return -1;
  gsl_matrix *m = gsl_matrix_alloc(1, (size_t)laws[law].npar);
  for (int i = 0; i < laws[law].npar; i++) gsl_matrix_set(m, 0, (size_t)i, par[i]);
  push_matrix_real(s, m);
  return 0;
}

/* ------------------------------------------------------------------ */
/* non-parametric: Kaplan-Meier, Nelson-Aalen, log-rank */

typedef struct { double t; int d; } Obs;
static int obs_cmp(const void *a, const void *b) {
  double x = ((const Obs *)a)->t, y = ((const Obs *)b)->t;
  return x < y ? -1 : x > y ? 1 : ((const Obs *)b)->d - ((const Obs *)a)->d;   /* events before censorings at a tie */
}

/* read times and event indicators (1 event, 0 censored) from depth 1 and 0 */
static int read_obs(const Stack *s, int depth, const char *what, Obs **out, size_t *n) {
  double *t, *d; size_t nt, nd;
  if (vector_at(s, depth + 1, what, &t, &nt) != 0) return -1;
  if (vector_at(s, depth, what, &d, &nd) != 0) { free(t); return -1; }
  if (nt != nd || nt == 0) { free(t); free(d); return mm_error("%s: times and event indicators must be vectors of the same length", what); }
  Obs *o = malloc(nt * sizeof *o);
  for (size_t i = 0; i < nt; i++) {
    if (t[i] < 0 || (d[i] != 0 && d[i] != 1)) { free(t); free(d); free(o); return mm_error("%s: times must be >= 0 and indicators 0 (censored) or 1 (event)", what); }
    o[i].t = t[i]; o[i].d = (int)d[i];
  }
  free(t); free(d);
  qsort(o, nt, sizeof *o, obs_cmp);
  *out = o; *n = nt;
  return 0;
}

/* times events -- M (t, S or H, at risk, events): one row per distinct event time */
static int estimator(Stack *s, const char *what, int aalen) {
  Obs *o; size_t n;
  if (read_obs(s, 0, what, &o, &n) != 0) return -1;
  size_t rows = 0;
  for (size_t i = 0; i < n; i++) if (o[i].d && (i == 0 || o[i].t != o[i - 1].t || !o[i - 1].d)) rows++;
  gsl_matrix *m = gsl_matrix_alloc(rows ? rows : 1, 4);
  double Sv = 1, Hv = 0; size_t r = 0;
  for (size_t i = 0; i < n;) {
    double t = o[i].t; size_t atrisk = n - i, d = 0, j = i;
    while (j < n && o[j].t == t) { d += (size_t)o[j].d; j++; }
    if (d) {
      Sv *= 1 - (double)d / (double)atrisk;
      Hv += (double)d / (double)atrisk;
      gsl_matrix_set(m, r, 0, t); gsl_matrix_set(m, r, 1, aalen ? Hv : Sv);
      gsl_matrix_set(m, r, 2, (double)atrisk); gsl_matrix_set(m, r, 3, (double)d);
      r++;
    }
    i = j;
  }
  if (rows == 0) { gsl_matrix_set(m, 0, 0, 0); gsl_matrix_set(m, 0, 1, aalen ? 0 : 1); gsl_matrix_set(m, 0, 2, (double)n); gsl_matrix_set(m, 0, 3, 0); }
  free(o);
  drop_n(s, 2);
  push_matrix_real(s, m);
  return 0;
}
static int op_km(Stack *s)          { return estimator(s, "km", 0); }
static int op_nelsonaalen(Stack *s) { return estimator(s, "nelsonaalen", 1); }

/* t1 e1 t2 e2 -- chi2 p: the log-rank test of two groups */
static int op_logrank(Stack *s) {
  Obs *a, *b; size_t na, nb;
  if (read_obs(s, 2, "logrank", &a, &na) != 0) return -1;
  if (read_obs(s, 0, "logrank", &b, &nb) != 0) { free(a); return -1; }
  /* merge the distinct event times */
  size_t N = na + nb;
  Obs *all = malloc(N * sizeof *all);
  memcpy(all, a, na * sizeof *a); memcpy(all + na, b, nb * sizeof *b);
  qsort(all, N, sizeof *all, obs_cmp);
  double O1 = 0, E1 = 0, V = 0;
  for (size_t i = 0; i < N;) {
    double t = all[i].t; size_t j = i;
    while (j < N && all[j].t == t) j++;
    size_t d1 = 0, d2 = 0, n1 = 0, n2 = 0;
    for (size_t k = 0; k < na; k++) { if (a[k].t >= t) n1++; if (a[k].t == t && a[k].d) d1++; }
    for (size_t k = 0; k < nb; k++) { if (b[k].t >= t) n2++; if (b[k].t == t && b[k].d) d2++; }
    size_t d = d1 + d2, n = n1 + n2;
    if (d > 0 && n > 1) {
      double e1 = (double)d * (double)n1 / (double)n;
      O1 += (double)d1; E1 += e1;
      V += (double)d * ((double)n1 / n) * ((double)n2 / n) * ((double)(n - d) / (double)(n - 1));
    }
    i = j;
  }
  free(a); free(b); free(all);
  if (V <= 0) return mm_error("logrank: no events to compare");
  double chi2 = (O1 - E1) * (O1 - E1) / V;
  drop_n(s, 4);
  push_real(s, chi2);
  push_real(s, gsl_cdf_chisq_Q(chi2, 1));
  return 0;
}

/* ------------------------------------------------------------------ */
/* maximum likelihood: fit the current law to censored data */

typedef struct { const Obs *o; size_t n; int l; } FitData;

/* negative log-likelihood in log-parameters (mu of lognormal untransformed) */
static double nll(const gsl_vector *x, void *q) {
  const FitData *fd = q;
  double p[5];
  for (int i = 0; i < laws[fd->l].npar; i++) p[i] = (fd->l == L_LOGNORMAL && i == 0) ? gsl_vector_get(x, i) : exp(gsl_vector_get(x, i));
  double ll = 0;
  for (size_t i = 0; i < fd->n; i++) {
    double t = fd->o[i].t, Sv = S_of(fd->l, p, t);
    if (Sv <= 0 || !isfinite(Sv)) return 1e300;
    ll += log(Sv);
    if (fd->o[i].d) { double h = h_of(fd->l, p, t); if (!(h > 0) || !isfinite(h)) return 1e300; ll += log(h); }
  }
  return -ll;
}

/* times events -- [params]: fits the current law (selected by name, parameters ignored) */
static int op_lifefit(Stack *s) {
  if (need_law("lifefit") != 0) return -1;
  Obs *o; size_t n;
  if (read_obs(s, 0, "lifefit", &o, &n) != 0) return -1;
  size_t events = 0; double sum = 0, lsum = 0, lsq = 0, tmax = 0;
  for (size_t i = 0; i < n; i++) { events += (size_t)o[i].d; sum += o[i].t; if (o[i].t > tmax) tmax = o[i].t; if (o[i].t > 0) { lsum += log(o[i].t); lsq += log(o[i].t) * log(o[i].t); } }
  if (events == 0) { free(o); return mm_error("lifefit: no events (every observation is censored)"); }
  double lambda = (double)events / (sum > 0 ? sum : 1), mean = sum / (double)n;
  double lm = lsum / (double)n, ls = sqrt(fmax(lsq / (double)n - lm * lm, 1e-4));
  int np = laws[law].npar;
  gsl_vector *x = gsl_vector_alloc((size_t)np), *step = gsl_vector_alloc((size_t)np);
  double start[5] = { 0 };
  switch (law) {
  case L_EXP:         start[0] = lambda; break;
  case L_WEIBULL:     start[0] = 1; start[1] = mean > 0 ? mean : 1; break;
  case L_GOMPERTZ:    start[0] = lambda / 4; start[1] = 2.0 / (tmax > 0 ? tmax : 1); break;
  case L_MAKEHAM:     start[0] = lambda / 10; start[1] = lambda / 4; start[2] = 2.0 / (tmax > 0 ? tmax : 1); break;
  case L_SILER:       start[0] = lambda; start[1] = 10.0 / (tmax > 0 ? tmax : 1); start[2] = lambda / 10; start[3] = lambda / 4; start[4] = 2.0 / (tmax > 0 ? tmax : 1); break;
  case L_LOGLOGISTIC: start[0] = mean > 0 ? mean : 1; start[1] = 2; break;
  case L_LOGNORMAL:   start[0] = lm; start[1] = ls; break;
  case L_GAMMA:       start[0] = 1; start[1] = mean > 0 ? mean : 1; break;
  }
  for (int i = 0; i < np; i++) {
    gsl_vector_set(x, (size_t)i, (law == L_LOGNORMAL && i == 0) ? start[i] : log(start[i]));
    gsl_vector_set(step, (size_t)i, 0.5);
  }
  FitData fd = { o, n, law };
  gsl_multimin_function F = { nll, (size_t)np, &fd };
  gsl_multimin_fminimizer *mn = gsl_multimin_fminimizer_alloc(gsl_multimin_fminimizer_nmsimplex2, (size_t)np);
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = gsl_multimin_fminimizer_set(mn, &F, x, step);
  int it = 0;
  for (int restart = 0; restart < 3 && st == GSL_SUCCESS; restart++) {
    for (it = 0; it < 5000; it++) {
      st = gsl_multimin_fminimizer_iterate(mn);
      if (st) break;
      if (gsl_multimin_test_size(gsl_multimin_fminimizer_size(mn), 1e-9) == GSL_SUCCESS) break;
    }
    gsl_vector_memcpy(x, mn->x);
    gsl_vector_set_all(step, 0.05);
    st = gsl_multimin_fminimizer_set(mn, &F, x, step);
  }
  gsl_set_error_handler(old);
  double fmin = mn->fval;
  for (int i = 0; i < np; i++) par[i] = (law == L_LOGNORMAL && i == 0) ? gsl_vector_get(mn->x, (size_t)i) : exp(gsl_vector_get(mn->x, (size_t)i));
  gsl_multimin_fminimizer_free(mn);
  gsl_vector_free(x); gsl_vector_free(step);
  free(o);
  if (!isfinite(fmin) || fmin >= 1e299) return mm_error("lifefit: the likelihood could not be maximised for %s", laws[law].name);
  num_evals = it;
  drop_n(s, 2);
  gsl_matrix *m = gsl_matrix_alloc(1, (size_t)np);
  for (int i = 0; i < np; i++) gsl_matrix_set(m, 0, (size_t)i, par[i]);
  push_matrix_real(s, m);
  push_real(s, -fmin);
  return 0;
}

/* ------------------------------------------------------------------ */
/* fitting from a life table: ages with qx, or ages with lx */

typedef struct { const double *x, *y; size_t n; int l; int from_lx; } TabFit;

/* least squares on log qx (qx spans orders of magnitude), or on log of
   the survivors relative to the first age */
static double table_rss(const gsl_vector *v, void *q) {
  const TabFit *tf = q;
  double p[5];
  for (int i = 0; i < laws[tf->l].npar; i++) p[i] = (tf->l == L_LOGNORMAL && i == 0) ? gsl_vector_get(v, (size_t)i) : exp(gsl_vector_get(v, (size_t)i));
  double rss = 0, S0 = S_of(tf->l, p, tf->x[0]);
  if (!(S0 > 0)) return 1e300;
  for (size_t i = 0; i < tf->n; i++) {
    double model;
    if (tf->from_lx) model = log(S_of(tf->l, p, tf->x[i]) / S0);
    else { double s = S_of(tf->l, p, tf->x[i]); model = s > 0 ? log(1 - S_of(tf->l, p, tf->x[i] + 1) / s) : 0; }
    if (!isfinite(model)) return 1e300;
    double d = model - tf->y[i];
    rss += d * d;
  }
  return rss;
}

static int fit_table(Stack *s, const char *what, int from_lx) {
  if (need_law(what) != 0) return -1;
  double *x, *y; size_t nx, ny;
  if (vector_at(s, 1, what, &x, &nx) != 0) return -1;
  if (vector_at(s, 0, what, &y, &ny) != 0) { free(x); return -1; }
  if (nx != ny || nx < (size_t)laws[law].npar) { free(x); free(y); return mm_error("%s: ages and values must be vectors of the same length, at least %d long", what, laws[law].npar); }
  for (size_t i = 0; i < nx; i++) {
    if (from_lx ? !(y[i] > 0) : !(y[i] > 0 && y[i] < 1)) { free(x); free(y); return mm_error("%s: %s", what, from_lx ? "lx must be > 0" : "qx must be strictly between 0 and 1"); }
    if (i && x[i] <= x[i - 1]) { free(x); free(y); return mm_error("%s: ages must increase", what); }
  }
  double *yl = malloc(nx * sizeof *yl);
  for (size_t i = 0; i < nx; i++) yl[i] = from_lx ? log(y[i] / y[0]) : log(y[i]);
  TabFit tf = { x, yl, nx, law, from_lx };
  int np = laws[law].npar;
  /* starting values from the crude slope of log qx or of -log lx */
  double q0 = from_lx ? (nx > 1 ? -yl[1] / (x[1] - x[0]) : 0.01) : y[0], span = x[nx - 1] - x[0];
  double q1 = from_lx ? (nx > 1 ? -(yl[nx - 1] - yl[nx - 2]) / (x[nx - 1] - x[nx - 2]) : q0) : y[nx - 1];
  if (q0 <= 0) q0 = 1e-4;
  if (q1 <= 0) q1 = q0;
  double c = span > 0 ? log(q1 / q0) / span : 0.08;
  if (!(c > 0.001)) c = 0.08;
  double start[5] = { 0 };
  switch (law) {
  case L_EXP:         start[0] = (q0 + q1) / 2; break;
  case L_WEIBULL:     start[0] = 3; start[1] = x[nx - 1]; break;
  case L_GOMPERTZ:    start[0] = q0 * exp(-c * x[0]); start[1] = c; break;
  case L_MAKEHAM:     start[0] = q0 / 2; start[1] = q0 * exp(-c * x[0]) / 2; start[2] = c; break;
  case L_SILER:       start[0] = q0; start[1] = 1; start[2] = q0 / 4; start[3] = q0 * exp(-c * x[0]) / 2; start[4] = c; break;
  case L_LOGLOGISTIC: start[0] = x[nx - 1]; start[1] = 4; break;
  case L_LOGNORMAL:   start[0] = log(x[nx - 1] > 0 ? x[nx - 1] : 1); start[1] = 0.5; break;
  case L_GAMMA:       start[0] = 4; start[1] = x[nx - 1] / 4; break;
  }
  gsl_vector *v = gsl_vector_alloc((size_t)np), *step = gsl_vector_alloc((size_t)np);
  for (int i = 0; i < np; i++) {
    gsl_vector_set(v, (size_t)i, (law == L_LOGNORMAL && i == 0) ? start[i] : log(start[i] > 0 ? start[i] : 1e-6));
    gsl_vector_set(step, (size_t)i, 0.5);
  }
  gsl_multimin_function F = { table_rss, (size_t)np, &tf };
  gsl_multimin_fminimizer *mn = gsl_multimin_fminimizer_alloc(gsl_multimin_fminimizer_nmsimplex2, (size_t)np);
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = gsl_multimin_fminimizer_set(mn, &F, v, step), it = 0;
  for (int restart = 0; restart < 4 && st == GSL_SUCCESS; restart++) {
    for (it = 0; it < 5000; it++) {
      st = gsl_multimin_fminimizer_iterate(mn);
      if (st) break;
      if (gsl_multimin_test_size(gsl_multimin_fminimizer_size(mn), 1e-10) == GSL_SUCCESS) break;
    }
    gsl_vector_memcpy(v, mn->x);
    gsl_vector_set_all(step, 0.05);
    st = gsl_multimin_fminimizer_set(mn, &F, v, step);
  }
  gsl_set_error_handler(old);
  double rss = table_rss(mn->x, &tf);
  double p[5];
  for (int i = 0; i < np; i++) p[i] = (law == L_LOGNORMAL && i == 0) ? gsl_vector_get(mn->x, (size_t)i) : exp(gsl_vector_get(mn->x, (size_t)i));
  gsl_multimin_fminimizer_free(mn);
  gsl_vector_free(v); gsl_vector_free(step);
  free(x); free(y); free(yl);
  if (!isfinite(rss) || rss >= 1e299) return mm_error("%s: no %s fits this table", what, laws[law].name);
  memcpy(par, p, sizeof par);
  num_evals = it;
  drop_n(s, 2);
  gsl_matrix *m = gsl_matrix_alloc(1, (size_t)np);
  for (int i = 0; i < np; i++) gsl_matrix_set(m, 0, (size_t)i, par[i]);
  push_matrix_real(s, m);
  push_real(s, rss);
  return 0;
}
static int op_lawfit(Stack *s)  { return fit_table(s, "lawfit", 0); }
static int op_lawfitl(Stack *s) { return fit_table(s, "lawfitl", 1); }

/* ------------------------------------------------------------------ */

static const Op table[] = {
  { "setlaw",   2, op_setlaw,   "Survival", "\"name\" [params] --", "select the lifetime law for the survival words: exponential [lambda], weibull [k lambda], gompertz [b c], makeham [a b c] (Gompertz-Makeham), siler [a1 b1 a2 a3 b3], loglogistic [alpha beta], lognormal [mu sigma], gamma [k theta]", "\"makeham\" [0.0005 0.00003 0.1] setlaw" },
  { "law",      0, op_law,      "Survival", "--",     "print the current law and its parameters", "law" },
  { "lawpar",   0, op_lawpar,   "Survival", "-- v",   "the parameters of the current law as a row vector (after lifefit, the estimates)", "\"gompertz\" [0.0001 0.09] setlaw lawpar" },
  { "surv",     1, op_surv,     "Survival", "t -- S", "survival function S(t) = P(T > t) under the current law; elementwise on a matrix of ages", "\"makeham\" [0.0005 0.00003 0.1] setlaw 65 surv" },
  { "haz",      1, op_haz,      "Survival", "t -- h", "hazard (force of mortality) h(t) = f(t)/S(t)", "\"gompertz\" [0.0001 0.09] setlaw 70 haz" },
  { "cumhaz",   1, op_cumhaz,   "Survival", "t -- H", "cumulative hazard H(t) = -ln S(t)", "\"gompertz\" [0.0001 0.09] setlaw 70 cumhaz" },
  { "lifepdf",  1, op_lifepdf,  "Survival", "t -- f", "density of the lifetime f(t) = h(t) S(t)", "\"gompertz\" [0.0001 0.09] setlaw 80 lifepdf" },
  { "lifecdf",  1, op_lifecdf,  "Survival", "t -- F", "distribution function F(t) = 1 - S(t)", "\"gompertz\" [0.0001 0.09] setlaw 80 lifecdf" },
  { "lifeinv",  1, op_lifeinv,  "Survival", "p -- t", "quantile: the t with F(t) = p", "\"gompertz\" [0.0001 0.09] setlaw 0.5 lifeinv" },
  { "lifemean", 0, op_lifemean, "Survival", "-- x",   "expected lifetime from birth, the integral of S", "\"gompertz\" [0.0001 0.09] setlaw lifemean" },
  { "lifemed",  0, op_lifemed,  "Survival", "-- x",   "median lifetime", "\"gompertz\" [0.0001 0.09] setlaw lifemed" },
  { "lifevar",  0, op_lifevar,  "Survival", "-- x",   "variance of the lifetime", "\"gompertz\" [0.0001 0.09] setlaw lifevar" },
  { "lifemode", 0, op_lifemode, "Survival", "-- x",   "modal age at death (for Gompertz, ln(c/b)/c)", "\"gompertz\" [0.0001 0.09] setlaw lifemode" },
  { "elife",    1, op_elife,    "Survival", "t -- e", "complete expectation of remaining life at t, e(t) = int_t^inf S / S(t); elementwise on a matrix", "\"gompertz\" [0.0001 0.09] setlaw 65 elife" },
  { "curtate",  1, op_curtate,  "Survival", "x -- e", "curtate expectation of life at x, the sum of kPx over k >= 1 (whole years)", "\"gompertz\" [0.0001 0.09] setlaw 65 curtate" },
  { "tpx",      2, op_tpx,      "Survival", "x n -- p", "nPx, probability that a life aged x survives n more years", "\"gompertz\" [0.0001 0.09] setlaw 65 10 tpx" },
  { "nqx",      2, op_nqx,      "Survival", "x n -- q", "nQx, probability that a life aged x dies within n years", "\"gompertz\" [0.0001 0.09] setlaw 65 10 nqx" },
  { "px",       1, op_px,       "Survival", "x -- p", "one-year survival probability at age x; elementwise on a matrix", "\"gompertz\" [0.0001 0.09] setlaw 65 px" },
  { "qx",       1, op_qx,       "Survival", "x -- q", "one-year death probability at age x; elementwise on a matrix", "\"gompertz\" [0.0001 0.09] setlaw 65 qx" },
  { "lx",       1, op_lx,       "Survival", "x -- l", "survivors at age x from a radix of 100000 at birth; elementwise on a matrix", "\"gompertz\" [0.0001 0.09] setlaw 65 lx" },
  { "lifetab",  2, op_lifetab,  "Survival", "x0 x1 -- M", "life table from x0 to x1: columns age, lx (radix 100000 at x0), dx, qx, ex (complete expectation)", "\"gompertz\" [0.0001 0.09] setlaw 60 65 lifetab" },
  { "ax",       2, op_ax,       "Survival", "x i -- a", "whole-life annuity-due: present value at i% a year of 1 a year while (x) lives, first payment now", "\"gompertz\" [0.0001 0.09] setlaw 65 3 ax" },
  { "axn",      3, op_axn,      "Survival", "x n i -- a", "n-year temporary annuity-due", "\"gompertz\" [0.0001 0.09] setlaw 65 20 3 axn" },
  { "Ax",       2, op_Ax,       "Survival", "x i -- A", "whole-life insurance: present value of 1 paid at the end of the year of death", "\"gompertz\" [0.0001 0.09] setlaw 65 3 Ax" },
  { "Axn",      3, op_Axn,      "Survival", "x n i -- A", "n-year term insurance", "\"gompertz\" [0.0001 0.09] setlaw 65 20 3 Axn" },
  { "km",       2, op_km,       "Survival", "t e -- M", "Kaplan-Meier: from times and event indicators (1 event, 0 censored), rows of time, S(t), number at risk, events", "[2 3 3 5 8 9] [1 1 0 1 1 0] km" },
  { "nelsonaalen", 2, op_nelsonaalen, "Survival", "t e -- M", "Nelson-Aalen cumulative hazard: rows of time, H(t), number at risk, events", "[2 3 3 5 8 9] [1 1 0 1 1 0] nelsonaalen" },
  { "logrank",  4, op_logrank,  "Survival", "t1 e1 t2 e2 -- chi2 p", "log-rank test that two groups share a survival curve", "[2 3 5 8] [1 1 1 0] [4 6 9 12] [1 1 1 1] logrank" },
  { "lawfit",   2, op_lawfit,   "Survival", "x q -- v rss", "fit the current law to a life table's one-year death probabilities qx at ages x, least squares on log qx; the parameters become current", "\"gompertz\" [1 1] setlaw [40 50 60 70 80] [0.002 0.004 0.009 0.022 0.055] lawfit" },
  { "lawfitl",  2, op_lawfitl,  "Survival", "x l -- v rss", "fit the current law to a life table's survivors lx at ages x (any radix), least squares on log lx/l0", "\"gompertz\" [1 1] setlaw [60 65 70 75 80] [100000 92000 81000 66000 47000] lawfitl" },
  { "lifefit",  2, op_lifefit,  "Survival", "t e -- v ll", "maximum-likelihood fit of the current law to times and event indicators (right censoring); leaves the parameters, which become current, and the log-likelihood", "\"exponential\" 1 setlaw [2 3 3 5 8 9] [1 1 0 1 1 0] lifefit" },
};

const Module mm41_mod_survival = {
  "survival", "1.1", "lifetime laws (Gompertz-Makeham and others), survival and hazard, life tables, annuities, Kaplan-Meier, fits",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
