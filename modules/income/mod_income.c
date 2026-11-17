/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The income module: the distributions used for incomes and wealth, their
 * moments, quantiles and shares, the inequality indices (Gini, Theil,
 * Atkinson, Lorenz curve, Palma and quantile ratios) and the poverty
 * measures (headcount, poverty gap, Foster-Greer-Thorbecke), each both for
 * a fitted distribution and for a data vector, and maximum-likelihood
 * fitting of any of the families to data.
 *
 * A current distribution is selected once, "dagum" [a b p] setdist, and the
 * parametric words then take only what they need. The families:
 *
 *   pareto        [xm alpha]   Pareto type I: F = 1 - (xm/x)^alpha, x >= xm
 *   lognormal     [mu sigma]   ln X ~ N(mu, sigma)
 *   dagum         [a b p]      F = (1 + (x/b)^-a)^-p
 *   singhmaddala  [a b q]      F = 1 - (1 + (x/b)^a)^-q   (Burr XII)
 *   fisk          [a b]        F = 1/(1 + (x/b)^-a)        (log-logistic)
 *   gamma         [k theta]    shape k, scale theta
 *   weibull       [k lambda]   F = 1 - exp(-(x/lambda)^k)
 *
 * Means, second moments, quantiles and Gini coefficients are closed forms
 * (the Dagum and Singh-Maddala ones through the beta function); the Lorenz
 * curve, Theil, Atkinson and the poverty gaps are integrals. The data
 * words end in d: ginid, theild, lorenzd, sharesd, headcountd, ... */

#include "module.h"
#include "errors.h"
#include "globals.h"
#include "ops.h"
#include "stack.h"
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_integration.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_multimin.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_sf_gamma.h>
#include <gsl/gsl_sort.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* the families */

enum { D_PARETO, D_LOGNORMAL, D_DAGUM, D_SINGHMADDALA, D_FISK, D_GAMMA, D_WEIBULL, D_COUNT };

typedef struct { const char *name; int npar; const char *pars; const char *form; } DistInfo;
static const DistInfo dists[D_COUNT] = {
  { "pareto",       2, "[xm alpha]", "F = 1 - (xm/x)^alpha, x >= xm" },
  { "lognormal",    2, "[mu sigma]", "ln X ~ N(mu, sigma)" },
  { "dagum",        3, "[a b p]",    "F = (1 + (x/b)^-a)^-p" },
  { "singhmaddala", 3, "[a b q]",    "F = 1 - (1 + (x/b)^a)^-q" },
  { "fisk",         2, "[a b]",      "F = 1/(1 + (x/b)^-a)" },
  { "gamma",        2, "[k theta]",  "shape k, scale theta" },
  { "weibull",      2, "[k lambda]", "F = 1 - exp(-(x/lambda)^k)" },
};

static int    dist = -1;
static double dp[3];

static int dist_from_name(const char *name) {
  for (int i = 0; i < D_COUNT; i++) if (strcmp(name, dists[i].name) == 0) return i;
  if (!strcmp(name, "burr") || !strcmp(name, "burr12") || !strcmp(name, "sm")) return D_SINGHMADDALA;
  if (!strcmp(name, "loglogistic")) return D_FISK;
  if (!strcmp(name, "lnorm")) return D_LOGNORMAL;
  return -1;
}

static int params_ok(int d, const double *p) {
  for (int i = 0; i < dists[d].npar; i++) if (!isfinite(p[i])) return 0;
  if (d == D_LOGNORMAL) return p[1] > 0;
  for (int i = 0; i < dists[d].npar; i++) if (p[i] <= 0) return 0;
  return 1;
}

static double pdf_of(int d, const double *p, double x) {
  if (x <= 0) return 0;
  switch (d) {
  case D_PARETO:       return x < p[0] ? 0 : p[1] * pow(p[0], p[1]) / pow(x, p[1] + 1);
  case D_LOGNORMAL:    return gsl_ran_lognormal_pdf(x, p[0], p[1]);
  case D_DAGUM:        { double u = pow(x / p[1], -p[0]); return p[0] * p[2] / x * u * pow(1 + u, -p[2] - 1); }
  case D_SINGHMADDALA: { double u = pow(x / p[1], p[0]); return p[0] * p[2] / x * u * pow(1 + u, -p[2] - 1); }
  case D_FISK:         { double u = pow(x / p[1], p[0]); return p[0] / x * u / ((1 + u) * (1 + u)); }
  case D_GAMMA:        return gsl_ran_gamma_pdf(x, p[0], p[1]);
  case D_WEIBULL:      return gsl_ran_weibull_pdf(x, p[1], p[0]);
  }
  return NAN;
}

static double cdf_of(int d, const double *p, double x) {
  if (x <= 0) return 0;
  switch (d) {
  case D_PARETO:       return x < p[0] ? 0 : 1 - pow(p[0] / x, p[1]);
  case D_LOGNORMAL:    return gsl_cdf_lognormal_P(x, p[0], p[1]);
  case D_DAGUM:        return pow(1 + pow(x / p[1], -p[0]), -p[2]);
  case D_SINGHMADDALA: return 1 - pow(1 + pow(x / p[1], p[0]), -p[2]);
  case D_FISK:         return 1 / (1 + pow(x / p[1], -p[0]));
  case D_GAMMA:        return gsl_cdf_gamma_P(x, p[0], p[1]);
  case D_WEIBULL:      return gsl_cdf_weibull_P(x, p[1], p[0]);
  }
  return NAN;
}

static double q_of(int d, const double *p, double u) {
  switch (d) {
  case D_PARETO:       return p[0] * pow(1 - u, -1 / p[1]);
  case D_LOGNORMAL:    return gsl_cdf_lognormal_Pinv(u, p[0], p[1]);
  case D_DAGUM:        return p[1] * pow(pow(u, -1 / p[2]) - 1, -1 / p[0]);
  case D_SINGHMADDALA: return p[1] * pow(pow(1 - u, -1 / p[2]) - 1, 1 / p[0]);
  case D_FISK:         return p[1] * pow(u / (1 - u), 1 / p[0]);
  case D_GAMMA:        return gsl_cdf_gamma_Pinv(u, p[0], p[1]);
  case D_WEIBULL:      return gsl_cdf_weibull_Pinv(u, p[1], p[0]);
  }
  return NAN;
}

/* E[X^r] where it exists, NAN otherwise */
static double moment_of(int d, const double *p, int r) {
  double a, b;
  switch (d) {
  case D_PARETO:       return p[1] > r ? p[1] * pow(p[0], r) / (p[1] - r) : NAN;
  case D_LOGNORMAL:    return exp(r * p[0] + 0.5 * r * r * p[1] * p[1]);
  case D_DAGUM:        a = p[0]; b = p[1]; return a > r ? pow(b, r) * gsl_sf_gamma(p[2] + r / a) * gsl_sf_gamma(1 - r / a) / gsl_sf_gamma(p[2]) : NAN;
  case D_SINGHMADDALA: a = p[0]; b = p[1]; return a * p[2] > r ? pow(b, r) * gsl_sf_gamma(1 + r / a) * gsl_sf_gamma(p[2] - r / a) / gsl_sf_gamma(p[2]) : NAN;
  case D_FISK:         a = p[0]; b = p[1]; return a > r ? pow(b, r) * (r * M_PI / a) / sin(r * M_PI / a) : NAN;
  case D_GAMMA:        return pow(p[1], r) * gsl_sf_gamma(p[0] + r) / gsl_sf_gamma(p[0]);
  case D_WEIBULL:      return pow(p[1], r) * gsl_sf_gamma(1 + (double)r / p[0]);
  }
  return NAN;
}

/* Gini in closed form, NAN where the mean does not exist */
static double gini_of(int d, const double *p) {
  double a;
  switch (d) {
  case D_PARETO:       return p[1] > 1 ? 1 / (2 * p[1] - 1) : NAN;
  case D_LOGNORMAL:    return 2 * gsl_cdf_ugaussian_P(p[1] / sqrt(2.0)) - 1;
  case D_DAGUM:        a = p[0]; return a > 1 ? gsl_sf_gamma(p[2]) * gsl_sf_gamma(2 * p[2] + 1 / a) / (gsl_sf_gamma(2 * p[2]) * gsl_sf_gamma(p[2] + 1 / a)) - 1 : NAN;
  case D_SINGHMADDALA: a = p[0]; return a * p[2] > 1 ? 1 - gsl_sf_gamma(p[2]) * gsl_sf_gamma(2 * p[2] - 1 / a) / (gsl_sf_gamma(p[2] - 1 / a) * gsl_sf_gamma(2 * p[2])) : NAN;
  case D_FISK:         return p[0] > 1 ? 1 / p[0] : NAN;
  case D_GAMMA:        return gsl_sf_gamma(p[0] + 0.5) / (gsl_sf_gamma(p[0] + 1) * sqrt(M_PI));
  case D_WEIBULL:      return 1 - pow(2, -1 / p[0]);
  }
  return NAN;
}

static int need_dist(const char *what) {
  if (dist < 0) return mm_error("%s: no distribution selected; \"dagum\" [a b p] setdist first (help income)", what);
  return 0;
}

#define PDF(x) pdf_of(dist, dp, (x))
#define CDF(x) cdf_of(dist, dp, (x))
#define QF(u)  q_of(dist, dp, (u))

/* ------------------------------------------------------------------ */
/* integrals over the current distribution */

typedef struct { double mu, z, eps; } IntArg;

static double xf(double x, void *q)      { (void)q; return x * PDF(x); }
static double theil_f(double x, void *q) { const IntArg *a = q; double r = x / a->mu; return r > 0 ? r * log(r) * PDF(x) : 0; }
static double mld_f(double x, void *q)   { const IntArg *a = q; double r = x / a->mu; return r > 0 ? -log(r) * PDF(x) : 0; }
static double atk_f(double x, void *q)   { const IntArg *a = q; double r = x / a->mu; return a->eps == 1 ? log(r) * PDF(x) : pow(r, 1 - a->eps) * PDF(x); }
static double fgt_f(double x, void *q)   { const IntArg *a = q; return pow((a->z - x) / a->z, a->eps) * PDF(x); }

static int integrate(double (*f)(double, void *), void *arg, double lo, double hi, double *out, const char *what) {
  gsl_integration_workspace *w = gsl_integration_workspace_alloc(2000);
  gsl_function F = { f, arg };
  double r, err;
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = isinf(hi) ? gsl_integration_qagiu(&F, lo, 0, 1e-9, 2000, w, &r, &err)
                     : gsl_integration_qags(&F, lo, hi, 0, 1e-9, 2000, w, &r, &err);
  gsl_set_error_handler(old);
  gsl_integration_workspace_free(w);
  if (st != GSL_SUCCESS && st != GSL_EROUND) return mm_error("%s: the integral did not converge (%s)", what, gsl_strerror(st));
  *out = r;
  return 0;
}

static double lower_bound(void) { return dist == D_PARETO ? dp[0] : 0; }

static int mean_or_error(double *mu, const char *what) {
  *mu = moment_of(dist, dp, 1);
  if (!isfinite(*mu)) return mm_error("%s: the mean of this %s does not exist (shape too small)", what, dists[dist].name);
  return 0;
}

/* L(p) = (1/mu) int_0^q(p) x f(x) dx */
static int lorenz_at(double p, double *out, const char *what) {
  if (!(p >= 0 && p <= 1)) return mm_error("%s: p must be between 0 and 1", what);
  if (p == 0) { *out = 0; return 0; }
  if (p == 1) { *out = 1; return 0; }
  double mu; if (mean_or_error(&mu, what) != 0) return -1;
  double integ; if (integrate(xf, NULL, lower_bound(), QF(p), &integ, what) != 0) return -1;
  *out = integ / mu;
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

typedef int (*xfn)(double x, double *out, const char *what);
static int pointwise(Stack *s, const char *what, xfn f) {
  if (need_dist(what) != 0) return -1;
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
  double x, r;
  if (pop_real_arg(s, what, &x) != 0) return -1;
  if (f(x, &r, what) != 0) return -1;
  push_real(s, r);
  return 0;
}

static int f_pdf(double x, double *o, const char *w) { (void)w; *o = PDF(x); return 0; }
static int f_cdf(double x, double *o, const char *w) { (void)w; *o = CDF(x); return 0; }
static int f_q(double u, double *o, const char *w)   { if (!(u > 0 && u < 1)) return mm_error("%s: p must be strictly between 0 and 1", w); *o = QF(u); return 0; }
static int f_lorenz(double p, double *o, const char *w) { return lorenz_at(p, o, w); }
static int f_head(double z, double *o, const char *w) { (void)w; *o = CDF(z); return 0; }

static int op_incpdf(Stack *s)  { return pointwise(s, "incpdf", f_pdf); }
static int op_inccdf(Stack *s)  { return pointwise(s, "inccdf", f_cdf); }
static int op_incq(Stack *s)    { return pointwise(s, "incq", f_q); }
static int op_lorenz(Stack *s)  { return pointwise(s, "lorenz", f_lorenz); }
static int op_headcount(Stack *s) { return pointwise(s, "headcount", f_head); }

static int push_summary(Stack *s, const char *what, double v, const char *missing) {
  if (!isfinite(v)) return mm_error("%s: %s", what, missing);
  push_real(s, v);
  return 0;
}

static int op_incmean(Stack *s) { if (need_dist("incmean") != 0) return -1; return push_summary(s, "incmean", moment_of(dist, dp, 1), "the mean does not exist (shape too small)"); }
static int op_incvar(Stack *s)  {
  if (need_dist("incvar") != 0) return -1;
  double m = moment_of(dist, dp, 1), m2 = moment_of(dist, dp, 2);
  return push_summary(s, "incvar", m2 - m * m, "the variance does not exist (shape too small)");
}
static int op_incmed(Stack *s)  { if (need_dist("incmed") != 0) return -1; push_real(s, QF(0.5)); return 0; }
static int op_incmode(Stack *s) {
  if (need_dist("incmode") != 0) return -1;
  double a, b, m;
  switch (dist) {
  case D_PARETO:       m = dp[0]; break;
  case D_LOGNORMAL:    m = exp(dp[0] - dp[1] * dp[1]); break;
  case D_DAGUM:        a = dp[0]; b = dp[1]; m = a * dp[2] > 1 ? b * pow((a * dp[2] - 1) / (a + 1), 1 / a) : 0; break;
  case D_SINGHMADDALA: a = dp[0]; b = dp[1]; m = a > 1 ? b * pow((a - 1) / (a * dp[2] + 1), 1 / a) : 0; break;
  case D_FISK:         a = dp[0]; b = dp[1]; m = a > 1 ? b * pow((a - 1) / (a + 1), 1 / a) : 0; break;
  case D_GAMMA:        m = dp[0] >= 1 ? (dp[0] - 1) * dp[1] : 0; break;
  case D_WEIBULL:      m = dp[0] > 1 ? dp[1] * pow((dp[0] - 1) / dp[0], 1 / dp[0]) : 0; break;
  default: m = NAN;
  }
  push_real(s, m); return 0;
}

static int op_gini(Stack *s) { if (need_dist("gini") != 0) return -1; return push_summary(s, "gini", gini_of(dist, dp), "the Gini coefficient does not exist (the mean is infinite)"); }

static int op_theil(Stack *s) {
  if (need_dist("theil") != 0) return -1;
  IntArg a = { 0, 0, 0 };
  if (mean_or_error(&a.mu, "theil") != 0) return -1;
  double T; if (integrate(theil_f, &a, lower_bound(), INFINITY, &T, "theil") != 0) return -1;
  push_real(s, T); return 0;
}

static int op_mld(Stack *s) {
  if (need_dist("mld") != 0) return -1;
  IntArg a = { 0, 0, 0 };
  if (mean_or_error(&a.mu, "mld") != 0) return -1;
  double T; if (integrate(mld_f, &a, lower_bound(), INFINITY, &T, "mld") != 0) return -1;
  push_real(s, T); return 0;
}

/* eps -- A */
static int op_atkinson(Stack *s) {
  if (need_dist("atkinson") != 0) return -1;
  IntArg a = { 0, 0, 0 };
  if (pop_real_arg(s, "atkinson", &a.eps) != 0) return -1;
  if (a.eps < 0) return mm_error("atkinson: the aversion parameter must be >= 0");
  if (mean_or_error(&a.mu, "atkinson") != 0) return -1;
  double integ; if (integrate(atk_f, &a, lower_bound(), INFINITY, &integ, "atkinson") != 0) return -1;
  push_real(s, a.eps == 1 ? 1 - exp(integ) : 1 - pow(integ, 1 / (1 - a.eps)));
  return 0;
}

/* p1 p2 -- share of total income going to those between the p1 and p2 quantiles */
static int op_share(Stack *s) {
  if (need_dist("share") != 0) return -1;
  double p1, p2;
  if (pop_two_reals(s, "share", &p1, &p2) != 0) return -1;
  if (!(p1 >= 0 && p2 <= 1 && p1 <= p2)) return mm_error("share: needs 0 <= p1 <= p2 <= 1");
  double l1, l2;
  if (lorenz_at(p1, &l1, "share") != 0 || lorenz_at(p2, &l2, "share") != 0) return -1;
  push_real(s, l2 - l1); return 0;
}

/* p -- share of the top fraction p (0.1: the top 10%) */
static int op_top(Stack *s) {
  if (need_dist("top") != 0) return -1;
  double p; if (pop_real_arg(s, "top", &p) != 0) return -1;
  if (!(p > 0 && p <= 1)) return mm_error("top: needs 0 < p <= 1");
  double l; if (lorenz_at(1 - p, &l, "top") != 0) return -1;
  push_real(s, 1 - l); return 0;
}

/* -- r: the Palma ratio, top 10% share over bottom 40% share */
static int op_palma(Stack *s) {
  if (need_dist("palma") != 0) return -1;
  double l9, l4;
  if (lorenz_at(0.9, &l9, "palma") != 0 || lorenz_at(0.4, &l4, "palma") != 0) return -1;
  if (l4 <= 0) return mm_error("palma: the bottom 40%% has no income");
  push_real(s, (1 - l9) / l4); return 0;
}

/* p1 p2 -- q(p1)/q(p2), e.g. 0.9 0.1 qratio for P90/P10 */
static int op_qratio(Stack *s) {
  if (need_dist("qratio") != 0) return -1;
  double p1, p2;
  if (pop_two_reals(s, "qratio", &p1, &p2) != 0) return -1;
  if (!(p1 > 0 && p1 < 1 && p2 > 0 && p2 < 1)) return mm_error("qratio: both p must be strictly between 0 and 1");
  push_real(s, QF(p1) / QF(p2)); return 0;
}

/* n -- M: the Lorenz curve at n+1 points, columns p, L(p) */
static int op_lorenzm(Stack *s) {
  if (need_dist("lorenzm") != 0) return -1;
  double n; if (pop_real_arg(s, "lorenzm", &n) != 0) return -1;
  if (n < 1 || n > 10000 || n != floor(n)) return mm_error("lorenzm: n must be a whole number from 1 to 10000");
  gsl_matrix *m = gsl_matrix_alloc((size_t)n + 1, 2);
  for (size_t i = 0; i <= (size_t)n; i++) {
    double p = (double)i / n, l;
    if (lorenz_at(p, &l, "lorenzm") != 0) { gsl_matrix_free(m); return -1; }
    gsl_matrix_set(m, i, 0, p); gsl_matrix_set(m, i, 1, l);
  }
  push_matrix_real(s, m); return 0;
}

/* -- v: decile shares as a row vector */
static int op_deciles(Stack *s) {
  if (need_dist("deciles") != 0) return -1;
  gsl_matrix *m = gsl_matrix_alloc(1, 10);
  double prev = 0;
  for (int i = 1; i <= 10; i++) {
    double l; if (lorenz_at(i / 10.0, &l, "deciles") != 0) { gsl_matrix_free(m); return -1; }
    gsl_matrix_set(m, 0, (size_t)i - 1, l - prev); prev = l;
  }
  push_matrix_real(s, m); return 0;
}

/* z -- gap: the poverty gap index FGT(1) at line z; z alpha -- FGT(alpha) */
static int fgt(Stack *s, const char *what, int with_alpha) {
  if (need_dist(what) != 0) return -1;
  IntArg a = { 0, 0, 1 };
  if (with_alpha && pop_real_arg(s, what, &a.eps) != 0) return -1;
  if (pop_real_arg(s, what, &a.z) != 0) return -1;
  if (a.z <= 0 || a.eps < 0) return mm_error("%s: needs a poverty line z > 0 (and alpha >= 0)", what);
  double integ;
  if (a.eps == 0) integ = CDF(a.z);
  else if (integrate(fgt_f, &a, lower_bound(), fmax(a.z, lower_bound()), &integ, what) != 0) return -1;
  push_real(s, integ); return 0;
}
static int op_povgap(Stack *s) { return fgt(s, "povgap", 0); }
static int op_fgt(Stack *s)    { return fgt(s, "fgt", 1); }

/* ------------------------------------------------------------------ */
/* the register */

static int op_setdist(Stack *s) {
  const stack_element *ne = &s->items[s->top - 1];
  if (ne->type != TYPE_STRING) return mm_error("setdist: needs \"name\" [params]; the families are pareto, lognormal, dagum, singhmaddala, fisk, gamma, weibull");
  int d = dist_from_name(ne->string);
  if (d < 0) return mm_error("setdist: no family '%s'; the families are pareto, lognormal, dagum, singhmaddala, fisk, gamma, weibull", ne->string);
  double *v; size_t n;
  if (vector_at(s, 0, "setdist", &v, &n) != 0) return -1;
  if ((int)n != dists[d].npar) { free(v); return mm_error("setdist: %s takes %d parameters %s", dists[d].name, dists[d].npar, dists[d].pars); }
  double p[3] = { 0 }; memcpy(p, v, n * sizeof *v); free(v);
  if (!params_ok(d, p)) return mm_error("setdist: the parameters of %s must be positive (mu of lognormal excepted)", dists[d].name);
  dist = d; memcpy(dp, p, sizeof dp);
  drop_n(s, 2);
  return 0;
}

static int op_dist(Stack *s) {
  (void)s;
  if (dist < 0) { printf("  no distribution selected; \"dagum\" [a b p] setdist, or incfit (help income)\n"); return 0; }
  printf("  %s %s   %s\n  parameters:", dists[dist].name, dists[dist].pars, dists[dist].form);
  for (int i = 0; i < dists[dist].npar; i++) printf(" %.10g", dp[i]);
  printf("\n");
  return 0;
}

static int op_distpar(Stack *s) {
  if (need_dist("distpar") != 0) return -1;
  gsl_matrix *m = gsl_matrix_alloc(1, (size_t)dists[dist].npar);
  for (int i = 0; i < dists[dist].npar; i++) gsl_matrix_set(m, 0, (size_t)i, dp[i]);
  push_matrix_real(s, m);
  return 0;
}

/* ------------------------------------------------------------------ */
/* the same measures from data: v -- x */

static int sorted_positive(const Stack *s, const char *what, double **out, size_t *n) {
  if (vector_at(s, 0, what, out, n) != 0) return -1;
  for (size_t i = 0; i < *n; i++) if (!((*out)[i] >= 0) || !isfinite((*out)[i])) { free(*out); return mm_error("%s: incomes must be finite and >= 0", what); }
  gsl_sort(*out, 1, *n);
  double sum = 0; for (size_t i = 0; i < *n; i++) sum += (*out)[i];
  if (sum <= 0) { free(*out); return mm_error("%s: total income is zero", what); }
  return 0;
}

static double mean_of(const double *v, size_t n) { double s = 0; for (size_t i = 0; i < n; i++) s += v[i]; return s / (double)n; }

static int op_ginid(Stack *s) {
  double *v; size_t n;
  if (sorted_positive(s, "ginid", &v, &n) != 0) return -1;
  double num = 0, sum = 0;
  for (size_t i = 0; i < n; i++) { num += (2.0 * (double)(i + 1) - (double)n - 1) * v[i]; sum += v[i]; }
  free(v);
  drop_n(s, 1);
  push_real(s, num / ((double)n * sum));
  return 0;
}

static int op_theild(Stack *s) {
  double *v; size_t n;
  if (sorted_positive(s, "theild", &v, &n) != 0) return -1;
  double mu = mean_of(v, n), T = 0;
  for (size_t i = 0; i < n; i++) if (v[i] > 0) T += (v[i] / mu) * log(v[i] / mu);
  free(v);
  drop_n(s, 1);
  push_real(s, T / (double)n);
  return 0;
}

static int op_mldd(Stack *s) {
  double *v; size_t n;
  if (sorted_positive(s, "mldd", &v, &n) != 0) return -1;
  double mu = mean_of(v, n), T = 0;
  for (size_t i = 0; i < n; i++) { if (v[i] <= 0) { free(v); return mm_error("mldd: every income must be > 0"); } T += log(mu / v[i]); }
  free(v);
  drop_n(s, 1);
  push_real(s, T / (double)n);
  return 0;
}

/* v eps -- A */
static int op_atkinsond(Stack *s) {
  double eps;
  if (pop_real_arg(s, "atkinsond", &eps) != 0) return -1;
  if (eps < 0) return mm_error("atkinsond: the aversion parameter must be >= 0");
  double *v; size_t n;
  if (sorted_positive(s, "atkinsond", &v, &n) != 0) return -1;
  double mu = mean_of(v, n), acc = 0;
  for (size_t i = 0; i < n; i++) {
    if (v[i] <= 0 && eps >= 1) { free(v); return mm_error("atkinsond: every income must be > 0 when eps >= 1"); }
    acc += eps == 1 ? log(v[i] / mu) : pow(v[i] / mu, 1 - eps);
  }
  acc /= (double)n;
  free(v);
  drop_n(s, 1);
  push_real(s, eps == 1 ? 1 - exp(acc) : 1 - pow(acc, 1 / (1 - eps)));
  return 0;
}

/* L(p) from data: the share of the poorest fraction p, interpolated */
static double lorenz_data(const double *v, size_t n, double p, double total) {
  double pos = p * (double)n; size_t k = (size_t)floor(pos);
  double acc = 0;
  for (size_t i = 0; i < k && i < n; i++) acc += v[i];
  if (k < n) acc += (pos - (double)k) * v[k];
  return acc / total;
}

/* v n -- M: Lorenz curve from data at n+1 points */
static int op_lorenzd(Stack *s) {
  double k; if (pop_real_arg(s, "lorenzd", &k) != 0) return -1;
  if (k < 1 || k > 10000 || k != floor(k)) return mm_error("lorenzd: n must be a whole number from 1 to 10000");
  double *v; size_t n;
  if (sorted_positive(s, "lorenzd", &v, &n) != 0) return -1;
  double total = mean_of(v, n) * (double)n;
  gsl_matrix *m = gsl_matrix_alloc((size_t)k + 1, 2);
  for (size_t i = 0; i <= (size_t)k; i++) { double p = (double)i / k; gsl_matrix_set(m, i, 0, p); gsl_matrix_set(m, i, 1, lorenz_data(v, n, p, total)); }
  free(v);
  drop_n(s, 1);
  push_matrix_real(s, m);
  return 0;
}

/* v -- row of decile shares */
static int op_decilesd(Stack *s) {
  double *v; size_t n;
  if (sorted_positive(s, "decilesd", &v, &n) != 0) return -1;
  double total = mean_of(v, n) * (double)n, prev = 0;
  gsl_matrix *m = gsl_matrix_alloc(1, 10);
  for (int i = 1; i <= 10; i++) { double l = lorenz_data(v, n, i / 10.0, total); gsl_matrix_set(m, 0, (size_t)i - 1, l - prev); prev = l; }
  free(v);
  drop_n(s, 1);
  push_matrix_real(s, m);
  return 0;
}

/* v p -- share of the top fraction p */
static int op_topd(Stack *s) {
  double p; if (pop_real_arg(s, "topd", &p) != 0) return -1;
  if (!(p > 0 && p <= 1)) return mm_error("topd: needs 0 < p <= 1");
  double *v; size_t n;
  if (sorted_positive(s, "topd", &v, &n) != 0) return -1;
  double total = mean_of(v, n) * (double)n, l = lorenz_data(v, n, 1 - p, total);
  free(v);
  drop_n(s, 1);
  push_real(s, 1 - l);
  return 0;
}

static int op_palmad(Stack *s) {
  double *v; size_t n;
  if (sorted_positive(s, "palmad", &v, &n) != 0) return -1;
  double total = mean_of(v, n) * (double)n, l9 = lorenz_data(v, n, 0.9, total), l4 = lorenz_data(v, n, 0.4, total);
  free(v);
  if (l4 <= 0) return mm_error("palmad: the bottom 40%% has no income");
  drop_n(s, 1);
  push_real(s, (1 - l9) / l4);
  return 0;
}

/* v z -- headcount ratio; v z -- poverty gap; v z alpha -- FGT */
static int fgt_data(Stack *s, const char *what, double alpha_fixed, int with_alpha) {
  double alpha = alpha_fixed, z;
  if (with_alpha && pop_real_arg(s, what, &alpha) != 0) return -1;
  if (pop_real_arg(s, what, &z) != 0) return -1;
  if (z <= 0 || alpha < 0) return mm_error("%s: needs a poverty line z > 0 (and alpha >= 0)", what);
  double *v; size_t n;
  if (sorted_positive(s, what, &v, &n) != 0) return -1;
  double acc = 0;
  for (size_t i = 0; i < n && v[i] < z; i++) acc += alpha == 0 ? 1 : pow((z - v[i]) / z, alpha);
  free(v);
  drop_n(s, 1);
  push_real(s, acc / (double)n);
  return 0;
}
static int op_headcountd(Stack *s) { return fgt_data(s, "headcountd", 0, 0); }
static int op_povgapd(Stack *s)    { return fgt_data(s, "povgapd", 1, 0); }
static int op_fgtd(Stack *s)       { return fgt_data(s, "fgtd", 0, 1); }

/* ------------------------------------------------------------------ */
/* maximum likelihood */

typedef struct { const double *v; size_t n; int d; } FitData;

static double nll(const gsl_vector *x, void *q) {
  const FitData *fd = q;
  double p[3];
  for (int i = 0; i < dists[fd->d].npar; i++) p[i] = (fd->d == D_LOGNORMAL && i == 0) ? gsl_vector_get(x, (size_t)i) : exp(gsl_vector_get(x, (size_t)i));
  double ll = 0;
  for (size_t i = 0; i < fd->n; i++) {
    double f = pdf_of(fd->d, p, fd->v[i]);
    if (!(f > 0) || !isfinite(f)) return 1e300;
    ll += log(f);
  }
  return -ll;
}

/* v -- [params] ll: fit the current family to data by maximum likelihood */
static int op_incfit(Stack *s) {
  if (need_dist("incfit") != 0) return -1;
  double *v; size_t n;
  if (sorted_positive(s, "incfit", &v, &n) != 0) return -1;
  if (n < 2) { free(v); return mm_error("incfit: needs at least two incomes"); }
  double mu = mean_of(v, n), lm = 0, lsq = 0; size_t np_ = 0;
  for (size_t i = 0; i < n; i++) if (v[i] > 0) { lm += log(v[i]); lsq += log(v[i]) * log(v[i]); np_++; }
  if (np_ < 2) { free(v); return mm_error("incfit: needs at least two positive incomes"); }
  lm /= (double)np_; double ls = sqrt(fmax(lsq / (double)np_ - lm * lm, 1e-4));
  int np = dists[dist].npar;
  double start[3] = { 0 };
  switch (dist) {
  case D_PARETO:       start[0] = v[0] > 0 ? v[0] : mu / 10; start[1] = 2; break;
  case D_LOGNORMAL:    start[0] = lm; start[1] = ls; break;
  case D_DAGUM:        start[0] = 3; start[1] = exp(lm); start[2] = 1; break;
  case D_SINGHMADDALA: start[0] = 3; start[1] = exp(lm); start[2] = 1; break;
  case D_FISK:         start[0] = 3; start[1] = exp(lm); break;
  case D_GAMMA:        start[0] = 2; start[1] = mu / 2; break;
  case D_WEIBULL:      start[0] = 1.5; start[1] = mu; break;
  }
  if (dist == D_PARETO) {
    /* closed form: xm = min, alpha = n / sum ln(x/xm) */
    double xm = v[0], acc = 0;
    if (xm <= 0) { free(v); return mm_error("incfit: pareto needs every income > 0"); }
    for (size_t i = 0; i < n; i++) acc += log(v[i] / xm);
    dp[0] = xm; dp[1] = acc > 0 ? (double)n / acc : INFINITY; dp[2] = 0;
    double ll = 0; for (size_t i = 0; i < n; i++) ll += log(pdf_of(dist, dp, v[i]));
    free(v);
    if (!isfinite(dp[1])) return mm_error("incfit: every income equals the minimum; no Pareto fits");
    drop_n(s, 1);
    gsl_matrix *m = gsl_matrix_alloc(1, 2); gsl_matrix_set(m, 0, 0, dp[0]); gsl_matrix_set(m, 0, 1, dp[1]);
    push_matrix_real(s, m); push_real(s, ll);
    return 0;
  }
  gsl_vector *x = gsl_vector_alloc((size_t)np), *step = gsl_vector_alloc((size_t)np);
  for (int i = 0; i < np; i++) {
    gsl_vector_set(x, (size_t)i, (dist == D_LOGNORMAL && i == 0) ? start[i] : log(start[i]));
    gsl_vector_set(step, (size_t)i, 0.5);
  }
  FitData fd = { v, n, dist };
  gsl_multimin_function F = { nll, (size_t)np, &fd };
  gsl_multimin_fminimizer *mn = gsl_multimin_fminimizer_alloc(gsl_multimin_fminimizer_nmsimplex2, (size_t)np);
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = gsl_multimin_fminimizer_set(mn, &F, x, step), it = 0;
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
  for (int i = 0; i < np; i++) dp[i] = (dist == D_LOGNORMAL && i == 0) ? gsl_vector_get(mn->x, (size_t)i) : exp(gsl_vector_get(mn->x, (size_t)i));
  gsl_multimin_fminimizer_free(mn);
  gsl_vector_free(x); gsl_vector_free(step);
  free(v);
  if (!isfinite(fmin) || fmin >= 1e299) return mm_error("incfit: the likelihood could not be maximised for %s", dists[dist].name);
  num_evals = it;
  drop_n(s, 1);
  gsl_matrix *m = gsl_matrix_alloc(1, (size_t)np);
  for (int i = 0; i < np; i++) gsl_matrix_set(m, 0, (size_t)i, dp[i]);
  push_matrix_real(s, m);
  push_real(s, -fmin);
  return 0;
}

/* ------------------------------------------------------------------ */
/* fitting from grouped data: Lorenz points or group shares */

/* The Lorenz curve is scale-free, so only the shape parameters are fitted
   and the scale (b, xm, mu, theta, lambda) keeps its current value. The
   shape slots per family: pareto {1}, lognormal {1}, dagum {0, 2},
   singhmaddala {0, 2}, fisk {0}, gamma {0}, weibull {0}. */
static const int shape_slots[D_COUNT][2] = { { 1, -1 }, { 1, -1 }, { 0, 2 }, { 0, 2 }, { 0, -1 }, { 0, -1 }, { 0, -1 } };

typedef struct { const double *p, *L; size_t n; int d; double par[3]; } LorenzFit;

typedef struct { int d; const double *par; } FamCtx;
static double xf_family(double x, void *q) { const FamCtx *c = q; return x * pdf_of(c->d, c->par, x); }

static double lorenz_model(int d, const double *par, double u) {
  if (u <= 0) return 0;
  if (u >= 1) return 1;
  double mu = moment_of(d, par, 1);
  if (!isfinite(mu) || mu <= 0) return NAN;
  double lo = d == D_PARETO ? par[0] : 0, hi = q_of(d, par, u);
  if (!isfinite(hi) || hi <= lo) return 0;
  /* integrate x f(x) from lo to q(u) with the current family's density */
  gsl_integration_workspace *w = gsl_integration_workspace_alloc(1000);
  FamCtx ctx = { d, par };
  double r, err;
  gsl_function F = { xf_family, &ctx };
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = gsl_integration_qags(&F, lo, hi, 0, 1e-8, 1000, w, &r, &err);
  gsl_set_error_handler(old);
  gsl_integration_workspace_free(w);
  return st == GSL_SUCCESS || st == GSL_EROUND ? r / mu : NAN;
}

static double lorenz_rss(const gsl_vector *x, void *q) {
  LorenzFit *lf = q;
  for (int k = 0; k < 2; k++) if (shape_slots[lf->d][k] >= 0) lf->par[shape_slots[lf->d][k]] = exp(gsl_vector_get(x, (size_t)k));
  if (!isfinite(moment_of(lf->d, lf->par, 1))) return 1e300;
  double rss = 0;
  for (size_t i = 0; i < lf->n; i++) {
    double L = lorenz_model(lf->d, lf->par, lf->p[i]);
    if (!isfinite(L)) return 1e300;
    rss += (L - lf->L[i]) * (L - lf->L[i]);
  }
  return rss;
}

/* [p] [L] -- [params] rss: least squares of the family's Lorenz curve on the points */
static int fit_lorenz(Stack *s, const char *what, const double *p, const double *L, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (!(p[i] >= 0 && p[i] <= 1 && L[i] >= 0 && L[i] <= p[i] + 1e-9)) return mm_error("%s: needs 0 <= p <= 1 and 0 <= L(p) <= p", what);
  int nshape = shape_slots[dist][1] >= 0 ? 2 : 1;
  LorenzFit lf = { p, L, n, dist, { 0 } };
  memcpy(lf.par, dp, sizeof lf.par);
  /* starting shapes that give a Gini of about 0.4 */
  double start[2] = { 2, 1 };
  switch (dist) {
  case D_PARETO: start[0] = 1.75; break;
  case D_LOGNORMAL: start[0] = 0.75; break;
  case D_DAGUM: case D_SINGHMADDALA: start[0] = 3; start[1] = 0.8; break;
  case D_FISK: start[0] = 2.5; break;
  case D_GAMMA: start[0] = 1.5; break;
  case D_WEIBULL: start[0] = 1.5; break;
  }
  gsl_vector *x = gsl_vector_alloc((size_t)nshape), *step = gsl_vector_alloc((size_t)nshape);
  for (int k = 0; k < nshape; k++) { gsl_vector_set(x, (size_t)k, log(start[k])); gsl_vector_set(step, (size_t)k, 0.3); }
  gsl_multimin_function F = { lorenz_rss, (size_t)nshape, &lf };
  gsl_multimin_fminimizer *mn = gsl_multimin_fminimizer_alloc(gsl_multimin_fminimizer_nmsimplex2, (size_t)nshape);
  gsl_error_handler_t *old = gsl_set_error_handler_off();
  int st = gsl_multimin_fminimizer_set(mn, &F, x, step), it = 0;
  for (int restart = 0; restart < 3 && st == GSL_SUCCESS; restart++) {
    for (it = 0; it < 3000; it++) {
      st = gsl_multimin_fminimizer_iterate(mn);
      if (st) break;
      if (gsl_multimin_test_size(gsl_multimin_fminimizer_size(mn), 1e-8) == GSL_SUCCESS) break;
    }
    gsl_vector_memcpy(x, mn->x);
    gsl_vector_set_all(step, 0.05);
    st = gsl_multimin_fminimizer_set(mn, &F, x, step);
  }
  gsl_set_error_handler(old);
  double rss = lorenz_rss(mn->x, &lf);
  gsl_multimin_fminimizer_free(mn);
  gsl_vector_free(x); gsl_vector_free(step);
  if (!isfinite(rss) || rss >= 1e299) return mm_error("%s: no %s fits these points", what, dists[dist].name);
  memcpy(dp, lf.par, sizeof dp);
  num_evals = it;
  gsl_matrix *m = gsl_matrix_alloc(1, (size_t)dists[dist].npar);
  for (int i = 0; i < dists[dist].npar; i++) gsl_matrix_set(m, 0, (size_t)i, dp[i]);
  push_matrix_real(s, m);
  push_real(s, rss);
  return 0;
}

static int op_incfitl(Stack *s) {
  if (need_dist("incfitl") != 0) return -1;
  double *p, *L; size_t np_, nl;
  if (vector_at(s, 1, "incfitl", &p, &np_) != 0) return -1;
  if (vector_at(s, 0, "incfitl", &L, &nl) != 0) { free(p); return -1; }
  if (np_ != nl || np_ == 0) { free(p); free(L); return mm_error("incfitl: p and L(p) must be vectors of the same length"); }
  drop_n(s, 2);
  int rc = fit_lorenz(s, "incfitl", p, L, np_);
  free(p); free(L);
  return rc;
}

/* [shares] -- [params] rss: the shares of k equal population groups (quintiles, deciles), summing to 1 or 100 */
static int op_incfits(Stack *s) {
  if (need_dist("incfits") != 0) return -1;
  double *sh; size_t k;
  if (vector_at(s, 0, "incfits", &sh, &k) != 0) return -1;
  if (k < 2) { free(sh); return mm_error("incfits: needs the shares of at least two groups"); }
  double total = 0; for (size_t i = 0; i < k; i++) { if (sh[i] < 0) { free(sh); return mm_error("incfits: shares must be >= 0"); } total += sh[i]; }
  if (total <= 0) { free(sh); return mm_error("incfits: the shares sum to zero"); }
  double *p = malloc((k - 1) * sizeof *p), *L = malloc((k - 1) * sizeof *L), acc = 0;
  for (size_t i = 0; i + 1 < k; i++) { acc += sh[i] / total; p[i] = (double)(i + 1) / (double)k; L[i] = acc; }
  free(sh);
  drop_n(s, 1);
  int rc = fit_lorenz(s, "incfits", p, L, k - 1);
  free(p); free(L);
  return rc;
}

/* ------------------------------------------------------------------ */

static const Op table[] = {
  { "setdist",  2, op_setdist,  "Income", "\"name\" [params] --", "select the income distribution for the parametric words: pareto [xm alpha], lognormal [mu sigma], dagum [a b p], singhmaddala [a b q] (Burr XII), fisk [a b], gamma [k theta], weibull [k lambda]", "\"dagum\" [3.5 20000 0.8] setdist" },
  { "dist",     0, op_dist,     "Income", "--",      "print the current distribution and its parameters", "dist" },
  { "distpar",  0, op_distpar,  "Income", "-- v",    "the parameters of the current distribution as a row vector (after incfit, the estimates)", "\"pareto\" [1000 2] setdist distpar" },
  { "incpdf",   1, op_incpdf,   "Income", "x -- f",  "density at income x; elementwise on a matrix", "\"pareto\" [1000 2] setdist 2000 incpdf" },
  { "inccdf",   1, op_inccdf,   "Income", "x -- F",  "share of the population with income below x; elementwise on a matrix", "\"pareto\" [1000 2] setdist 2000 inccdf" },
  { "incq",     1, op_incq,     "Income", "p -- x",  "quantile: the income below which a fraction p lies; elementwise on a matrix", "\"pareto\" [1000 2] setdist 0.9 incq" },
  { "incmean",  0, op_incmean,  "Income", "-- x",    "mean income", "\"pareto\" [1000 2] setdist incmean" },
  { "incvar",   0, op_incvar,   "Income", "-- x",    "variance of income", "\"lognormal\" [10 0.5] setdist incvar" },
  { "incmed",   0, op_incmed,   "Income", "-- x",    "median income", "\"pareto\" [1000 2] setdist incmed" },
  { "incmode",  0, op_incmode,  "Income", "-- x",    "modal income", "\"lognormal\" [10 0.5] setdist incmode" },
  { "gini",     0, op_gini,     "Income", "-- G",    "Gini coefficient of the current distribution (closed form)", "\"pareto\" [1000 2] setdist gini" },
  { "theil",    0, op_theil,    "Income", "-- T",    "Theil index (GE(1)) of the current distribution", "\"lognormal\" [10 0.5] setdist theil" },
  { "mld",      0, op_mld,      "Income", "-- L",    "mean log deviation (GE(0)) of the current distribution", "\"lognormal\" [10 0.5] setdist mld" },
  { "atkinson", 1, op_atkinson, "Income", "eps -- A", "Atkinson index with inequality aversion eps (1 is allowed)", "\"lognormal\" [10 0.5] setdist 1 atkinson" },
  { "lorenz",   1, op_lorenz,   "Income", "p -- L",  "Lorenz curve: the share of total income held by the poorest fraction p; elementwise on a matrix", "\"pareto\" [1000 2] setdist 0.5 lorenz" },
  { "lorenzm",  1, op_lorenzm,  "Income", "n -- M",  "the Lorenz curve at n+1 points, columns p and L(p)", "\"pareto\" [1000 2] setdist 10 lorenzm" },
  { "share",    2, op_share,    "Income", "p1 p2 -- s", "share of total income going to those between the p1 and p2 quantiles", "\"pareto\" [1000 2] setdist 0.4 0.6 share" },
  { "top",      1, op_top,      "Income", "p -- s",  "share of total income held by the top fraction p (0.1 is the top 10%)", "\"pareto\" [1000 2] setdist 0.1 top" },
  { "deciles",  0, op_deciles,  "Income", "-- v",    "decile shares of total income as a row vector", "\"lognormal\" [10 0.5] setdist deciles" },
  { "palma",    0, op_palma,    "Income", "-- r",    "Palma ratio: the top 10% share over the bottom 40% share", "\"lognormal\" [10 0.5] setdist palma" },
  { "qratio",   2, op_qratio,   "Income", "p1 p2 -- r", "ratio of quantiles, 0.9 0.1 qratio is P90/P10", "\"lognormal\" [10 0.5] setdist 0.9 0.1 qratio" },
  { "headcount", 1, op_headcount, "Income", "z -- H", "poverty headcount ratio at poverty line z (FGT alpha = 0); elementwise on a matrix", "\"lognormal\" [10 0.5] setdist 10000 headcount" },
  { "povgap",   1, op_povgap,   "Income", "z -- P",  "poverty gap index at line z (FGT alpha = 1)", "\"lognormal\" [10 0.5] setdist 10000 povgap" },
  { "fgt",      2, op_fgt,      "Income", "z alpha -- P", "Foster-Greer-Thorbecke index at line z with aversion alpha (0 headcount, 1 gap, 2 severity)", "\"lognormal\" [10 0.5] setdist 10000 2 fgt" },
  { "incfit",   1, op_incfit,   "Income", "v -- v2 ll", "maximum-likelihood fit of the current family to a vector of incomes; leaves the parameters, which become current, and the log-likelihood", "\"lognormal\" [0 1] setdist [1200 1500 2200 3100 4000 5400 9000 15000] incfit" },
  { "incfitl",  2, op_incfitl,  "Income", "p L -- v ll", "fit the shape of the current family to points of a Lorenz curve by least squares (the scale is kept); leaves the parameters, which become current, and the residual sum of squares", "\"dagum\" [3 1 1] setdist [0.2 0.4 0.6 0.8] [0.031 0.114 0.255 0.481] incfitl" },
  { "incfits",  1, op_incfits,  "Income", "v -- v2 ll", "fit the current family to the income shares of equal population groups (quintiles, deciles; summing to 1 or 100), as incfitl on the cumulative shares", "\"dagum\" [3 1 1] setdist [3.1 8.3 14.1 22.7 51.9] incfits" },
  { "ginid",    1, op_ginid,    "Income", "v -- G",  "Gini coefficient of a vector of incomes", "[1 2 3 4 10] ginid" },
  { "theild",   1, op_theild,   "Income", "v -- T",  "Theil index of a vector of incomes", "[1 2 3 4 10] theild" },
  { "mldd",     1, op_mldd,     "Income", "v -- L",  "mean log deviation of a vector of incomes", "[1 2 3 4 10] mldd" },
  { "atkinsond", 2, op_atkinsond, "Income", "v eps -- A", "Atkinson index of a vector of incomes", "[1 2 3 4 10] 0.5 atkinsond" },
  { "lorenzd",  2, op_lorenzd,  "Income", "v n -- M", "Lorenz curve of a vector of incomes at n+1 points", "[1 2 3 4 10] 5 lorenzd" },
  { "decilesd", 1, op_decilesd, "Income", "v -- v2", "decile shares of a vector of incomes", "[1 2 3 4 5 6 7 8 9 10] decilesd" },
  { "topd",     2, op_topd,     "Income", "v p -- s", "share of total income held by the top fraction p of a vector", "[1 2 3 4 10] 0.2 topd" },
  { "palmad",   1, op_palmad,   "Income", "v -- r",  "Palma ratio of a vector of incomes", "[1 2 3 4 5 6 7 8 9 10] palmad" },
  { "headcountd", 2, op_headcountd, "Income", "v z -- H", "poverty headcount ratio of a vector at line z", "[1 2 3 4 10] 2.5 headcountd" },
  { "povgapd",  2, op_povgapd,  "Income", "v z -- P", "poverty gap index of a vector at line z", "[1 2 3 4 10] 2.5 povgapd" },
  { "fgtd",     3, op_fgtd,     "Income", "v z alpha -- P", "Foster-Greer-Thorbecke index of a vector at line z with aversion alpha", "[1 2 3 4 10] 2.5 2 fgtd" },
};

const Module mm41_mod_income = {
  "income", "1.1", "income distributions (Dagum, Singh-Maddala, Pareto, lognormal and more), inequality and poverty measures, fits",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
