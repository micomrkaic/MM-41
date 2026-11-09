/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The stat module: the HP-41 Stat Pac and the Advantage Pac's curve
 * fitting, on vectors and matrices. A vector is a real matrix with one row
 * or one column. Fits return the coefficients and R² as a row vector;
 * `ols` is multiple regression with an intercept added for you. The
 * distribution words give the CDF and its inverse for t, chi-square, F,
 * binomial and Poisson (the normal is in the special module: ncdf, nquant). */

#include "module.h"
#include "errors.h"
#include "ops.h"
#include "stack.h"
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_sort.h>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- vectors on the stack ---- */

static int is_vector(const stack_element *e) {
  return e->type == TYPE_MATRIX_REAL && (e->matrix_real->size1 == 1 || e->matrix_real->size2 == 1);
}

/* Copy the vector at depth into a fresh array; the caller frees it. */
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

static int push_row(Stack *s, const double *v, size_t n) {
  gsl_matrix *m = gsl_matrix_alloc(1, n);
  if (!m) return mm_error("out of memory");
  for (size_t i = 0; i < n; i++) gsl_matrix_set(m, 0, i, v[i]);
  push_matrix_real(s, m);
  return 0;
}

static int push_col(Stack *s, const double *v, size_t n) {
  gsl_matrix *m = gsl_matrix_alloc(n, 1);
  if (!m) return mm_error("out of memory");
  for (size_t i = 0; i < n; i++) gsl_matrix_set(m, i, 0, v[i]);
  push_matrix_real(s, m);
  return 0;
}

/* ---- descriptive statistics: v -- x ---- */

typedef double (*vfn)(const double *, size_t);
static int reduce(Stack *s, const char *what, size_t min_n, vfn f) {
  double *v; size_t n;
  if (vector_at(s, 0, what, &v, &n) != 0) return -1;
  if (n < min_n) { free(v); return mm_error("%s: needs at least %zu values", what, min_n); }
  double r = f(v, n);
  free(v);
  drop_n(s, 1);
  push_real(s, r);
  return 0;
}

static double f_mean(const double *v, size_t n)   { return gsl_stats_mean(v, 1, n); }
static double f_sd(const double *v, size_t n)     { return gsl_stats_sd(v, 1, n); }
static double f_sem(const double *v, size_t n)    { return gsl_stats_sd(v, 1, n) / sqrt((double)n); }
static double f_skew(const double *v, size_t n)   { return gsl_stats_skew(v, 1, n); }
static double f_kurt(const double *v, size_t n)   { return gsl_stats_kurtosis(v, 1, n); }
static double f_median(const double *v, size_t n) {
  double *c = malloc(n * sizeof *c);
  memcpy(c, v, n * sizeof *c);
  gsl_sort(c, 1, n);
  double r = gsl_stats_median_from_sorted_data(c, 1, n);
  free(c);
  return r;
}

static int op_mean(Stack *s)   { return reduce(s, "mean", 1, f_mean); }
static int op_sd(Stack *s)     { return reduce(s, "sd", 2, f_sd); }
static int op_sem(Stack *s)    { return reduce(s, "sem", 2, f_sem); }
static int op_median(Stack *s) { return reduce(s, "median", 1, f_median); }
static int op_skew(Stack *s)   { return reduce(s, "skew", 3, f_skew); }
static int op_kurt(Stack *s)   { return reduce(s, "kurt", 4, f_kurt); }

/* v p -- q */
static int op_quantile(Stack *s) {
  double p;
  stack_coerce_real(s, 0);
  if (s->items[s->top].type != TYPE_REAL) return mm_error("quantile: needs a vector and a probability");
  p = s->items[s->top].real;
  if (!(p >= 0 && p <= 1)) return mm_error("quantile: the probability must be in [0, 1]");
  double *v; size_t n;
  if (vector_at(s, 1, "quantile", &v, &n) != 0) return -1;
  gsl_sort(v, 1, n);
  double q = gsl_stats_quantile_from_sorted_data(v, 1, n, p);
  free(v);
  drop_n(s, 2);
  push_real(s, q);
  return 0;
}

/* x y -- r (two vectors of the same length) */
static int two_vectors(Stack *s, const char *what, double **x, double **y, size_t *n) {
  size_t nx, ny;
  if (vector_at(s, 1, what, x, &nx) != 0) return -1;
  if (vector_at(s, 0, what, y, &ny) != 0) { free(*x); return -1; }
  if (nx != ny) { free(*x); free(*y); return mm_error("%s: the vectors have %zu and %zu elements", what, nx, ny); }
  *n = nx;
  return 0;
}

static int op_cov(Stack *s) {
  double *x, *y; size_t n;
  if (two_vectors(s, "cov", &x, &y, &n) != 0) return -1;
  if (n < 2) { free(x); free(y); return mm_error("cov: needs at least 2 pairs"); }
  double r = gsl_stats_covariance(x, 1, y, 1, n);
  free(x); free(y); drop_n(s, 2); push_real(s, r);
  return 0;
}

static int op_corr(Stack *s) {
  double *x, *y; size_t n;
  if (two_vectors(s, "corr", &x, &y, &n) != 0) return -1;
  if (n < 2) { free(x); free(y); return mm_error("corr: needs at least 2 pairs"); }
  double r = gsl_stats_correlation(x, 1, y, 1, n);
  free(x); free(y); drop_n(s, 2); push_real(s, r);
  return 0;
}

/* v n -- v2 : trailing moving average, n values; the first n-1 are nan-free partial means */
static int op_movavg(Stack *s) {
  double w;
  if (pop_real_arg(s, "movavg", &w) != 0) return -1;
  if (w != floor(w) || w < 1) { push_real(s, w); return mm_error("movavg: the window must be a whole number >= 1"); }
  double *v; size_t n;
  if (vector_at(s, 0, "movavg", &v, &n) != 0) { push_real(s, w); return -1; }
  size_t k = (size_t)w;
  double *r = malloc(n * sizeof *r);
  double sum = 0;
  for (size_t i = 0; i < n; i++) {
    sum += v[i];
    if (i >= k) sum -= v[i - k];
    r[i] = sum / (double)(i + 1 < k ? i + 1 : k);
  }
  free(v);
  drop_n(s, 1);
  int rc = push_col(s, r, n);
  free(r);
  return rc;
}

/* v k -- M : histogram with k equal bins over [min, max]: columns centre, count */
static int op_hist(Stack *s) {
  double kd;
  if (pop_real_arg(s, "hist", &kd) != 0) return -1;
  if (kd != floor(kd) || kd < 1 || kd > 100000) { push_real(s, kd); return mm_error("hist: the number of bins must be a whole number >= 1"); }
  double *v; size_t n;
  if (vector_at(s, 0, "hist", &v, &n) != 0) { push_real(s, kd); return -1; }
  size_t k = (size_t)kd;
  double lo = gsl_stats_min(v, 1, n), hi = gsl_stats_max(v, 1, n);
  if (hi == lo) hi = lo + 1;
  gsl_matrix *m = gsl_matrix_calloc(k, 2);
  double width = (hi - lo) / (double)k;
  for (size_t b = 0; b < k; b++) gsl_matrix_set(m, b, 0, lo + ((double)b + 0.5) * width);
  for (size_t i = 0; i < n; i++) {
    size_t b = (size_t)((v[i] - lo) / width);
    if (b >= k) b = k - 1;
    gsl_matrix_set(m, b, 1, gsl_matrix_get(m, b, 1) + 1);
  }
  free(v);
  drop_n(s, 1);
  push_matrix_real(s, m);
  return 0;
}

/* ---- curve fitting: x y -- [a b r2] ---- */

/* Fit y' = a' + b' x' on transformed data; the transform is what the Advantage
   Pac's curve fit did: linear, exponential (ln y), logarithmic (ln x),
   power (ln x, ln y). R² is reported on the transformed scale. */
typedef enum { FIT_LIN, FIT_EXP, FIT_LOG, FIT_POW } FitKind;

static int fit(Stack *s, const char *what, FitKind kind) {
  double *x, *y; size_t n;
  if (two_vectors(s, what, &x, &y, &n) != 0) return -1;
  if (n < 2) { free(x); free(y); return mm_error("%s: needs at least 2 points", what); }
  for (size_t i = 0; i < n; i++) {
    if ((kind == FIT_LOG || kind == FIT_POW) && x[i] <= 0) { free(x); free(y); return mm_error("%s: x must be positive", what); }
    if ((kind == FIT_EXP || kind == FIT_POW) && y[i] <= 0) { free(x); free(y); return mm_error("%s: y must be positive", what); }
    if (kind == FIT_LOG || kind == FIT_POW) x[i] = log(x[i]);
    if (kind == FIT_EXP || kind == FIT_POW) y[i] = log(y[i]);
  }
  double c0, c1, cov00, cov01, cov11, sumsq;
  gsl_fit_linear(x, 1, y, 1, n, &c0, &c1, &cov00, &cov01, &cov11, &sumsq);
  double tss = gsl_stats_tss(y, 1, n);
  double r2 = tss > 0 ? 1.0 - sumsq / tss : 1.0;
  free(x); free(y);
  if (kind == FIT_EXP || kind == FIT_POW) c0 = exp(c0);      /* a = e^{a'} */
  double out[3] = { c0, c1, r2 };
  drop_n(s, 2);
  return push_row(s, out, 3);
}

static int op_linfit(Stack *s) { return fit(s, "linfit", FIT_LIN); }
static int op_expfit(Stack *s) { return fit(s, "expfit", FIT_EXP); }
static int op_logfit(Stack *s) { return fit(s, "logfit", FIT_LOG); }
static int op_powfit(Stack *s) { return fit(s, "powfit", FIT_POW); }

/* ---- multiple regression: X y -- beta  /  X y -- M r2 ---- */

/* X: N x k real matrix of regressors (an intercept column is added first),
   y: a vector of N. */
static int ols_core(Stack *s, const char *what, gsl_vector **beta_out, gsl_matrix **cov_out, double *r2, size_t *N, size_t *k) {
  const stack_element *ex = &s->items[s->top - 1];
  if (ex->type != TYPE_MATRIX_REAL) return mm_error("%s: needs the regressor matrix X (N x k) and the vector y", what);
  double *y; size_t n;
  if (vector_at(s, 0, what, &y, &n) != 0) return -1;
  const gsl_matrix *X = ex->matrix_real;
  size_t p = X->size2 + 1;
  if (X->size1 != n) { free(y); return mm_error("%s: X has %zu rows but y has %zu values", what, X->size1, n); }
  if (n <= p) { free(y); return mm_error("%s: needs more observations (%zu) than coefficients (%zu)", what, n, p); }
  gsl_matrix *Xi = gsl_matrix_alloc(n, p);
  for (size_t i = 0; i < n; i++) {
    gsl_matrix_set(Xi, i, 0, 1.0);
    for (size_t j = 1; j < p; j++) gsl_matrix_set(Xi, i, j, gsl_matrix_get(X, i, j - 1));
  }
  gsl_vector_view yv = gsl_vector_view_array(y, n);
  gsl_vector *beta = gsl_vector_alloc(p);
  gsl_matrix *cov = gsl_matrix_alloc(p, p);
  gsl_multifit_linear_workspace *ws = gsl_multifit_linear_alloc(n, p);
  double chisq;
  int st = gsl_multifit_linear(Xi, &yv.vector, beta, cov, &chisq, ws);
  gsl_multifit_linear_free(ws);
  gsl_matrix_free(Xi);
  double tss = gsl_stats_tss(y, 1, n);
  free(y);
  if (st != 0) { gsl_vector_free(beta); gsl_matrix_free(cov); return mm_error("%s: the regression is singular", what); }
  /* gsl's covariance assumes unit error variance; scale by s^2 = RSS/(N-p) */
  double s2 = chisq / (double)(n - p);
  gsl_matrix_scale(cov, s2);
  *r2 = tss > 0 ? 1.0 - chisq / tss : 1.0;
  *beta_out = beta; *cov_out = cov; *N = n; *k = p;
  return 0;
}

static int op_ols(Stack *s) {
  gsl_vector *beta; gsl_matrix *cov; double r2; size_t N, k;
  if (ols_core(s, "ols", &beta, &cov, &r2, &N, &k) != 0) return -1;
  gsl_matrix *out = gsl_matrix_alloc(k, 1);
  for (size_t j = 0; j < k; j++) gsl_matrix_set(out, j, 0, gsl_vector_get(beta, j));
  gsl_vector_free(beta); gsl_matrix_free(cov);
  drop_n(s, 2);
  push_matrix_real(s, out);
  return 0;
}

/* columns: coefficient, standard error, t, two-sided p */
static int op_olstab(Stack *s) {
  gsl_vector *beta; gsl_matrix *cov; double r2; size_t N, k;
  if (ols_core(s, "olstab", &beta, &cov, &r2, &N, &k) != 0) return -1;
  gsl_matrix *out = gsl_matrix_alloc(k, 4);
  double df = (double)(N - k);
  for (size_t j = 0; j < k; j++) {
    double b = gsl_vector_get(beta, j), se = sqrt(gsl_matrix_get(cov, j, j));
    double t = se > 0 ? b / se : 0.0;
    double pval = se > 0 ? 2.0 * gsl_cdf_tdist_Q(fabs(t), df) : 0.0;
    gsl_matrix_set(out, j, 0, b); gsl_matrix_set(out, j, 1, se);
    gsl_matrix_set(out, j, 2, t); gsl_matrix_set(out, j, 3, pval);
  }
  gsl_vector_free(beta); gsl_matrix_free(cov);
  drop_n(s, 2);
  push_matrix_real(s, out);
  push_real(s, r2);
  return 0;
}

/* ---- distributions ---- */

static int two_args(Stack *s, const char *what, double *a, double *b) { return pop_two_reals(s, what, a, b); }

static int three_args(Stack *s, const char *what, double *a, double *b, double *c) {
  for (int d = 0; d < 3; d++) {
    stack_coerce_real(s, d);
    if (s->items[s->top - d].type != TYPE_REAL) return mm_error("%s: needs three real numbers", what);
  }
  *a = s->items[s->top - 2].real; *b = s->items[s->top - 1].real; *c = s->items[s->top].real;
  pop(s); pop(s); pop(s);
  return 0;
}

static int whole(const char *what, const char *which, double v, double lo) {
  if (v != floor(v) || v < lo) return mm_error("%s: %s must be a whole number >= %g", what, which, lo);
  return 0;
}

static int prob_ok(const char *what, double p) {
  if (!(p > 0 && p < 1)) return mm_error("%s: the probability must be strictly between 0 and 1", what);
  return 0;
}

/* x df tcdf, p df tinv */
static int op_tcdf(Stack *s) {
  double x, df; if (two_args(s, "tcdf", &x, &df) != 0) return -1;
  if (df <= 0) { push_real(s, x); push_real(s, df); return mm_error("tcdf: df must be positive"); }
  push_real(s, gsl_cdf_tdist_P(x, df)); return 0;
}
static int op_tinv(Stack *s) {
  double p, df; if (two_args(s, "tinv", &p, &df) != 0) return -1;
  if (prob_ok("tinv", p) != 0 || df <= 0) { push_real(s, p); push_real(s, df); return df <= 0 ? mm_error("tinv: df must be positive") : -1; }
  push_real(s, gsl_cdf_tdist_Pinv(p, df)); return 0;
}
static int op_chi2cdf(Stack *s) {
  double x, df; if (two_args(s, "chi2cdf", &x, &df) != 0) return -1;
  if (df <= 0 || x < 0) { push_real(s, x); push_real(s, df); return mm_error("chi2cdf: needs x >= 0 and df > 0"); }
  push_real(s, gsl_cdf_chisq_P(x, df)); return 0;
}
static int op_chi2inv(Stack *s) {
  double p, df; if (two_args(s, "chi2inv", &p, &df) != 0) return -1;
  if (prob_ok("chi2inv", p) != 0 || df <= 0) { push_real(s, p); push_real(s, df); return df <= 0 ? mm_error("chi2inv: df must be positive") : -1; }
  push_real(s, gsl_cdf_chisq_Pinv(p, df)); return 0;
}
static int op_fcdf(Stack *s) {
  double x, d1, d2; if (three_args(s, "fcdf", &x, &d1, &d2) != 0) return -1;
  if (x < 0 || d1 <= 0 || d2 <= 0) { push_real(s, x); push_real(s, d1); push_real(s, d2); return mm_error("fcdf: needs x >= 0 and positive degrees of freedom"); }
  push_real(s, gsl_cdf_fdist_P(x, d1, d2)); return 0;
}
static int op_finv(Stack *s) {
  double p, d1, d2; if (three_args(s, "finv", &p, &d1, &d2) != 0) return -1;
  if (prob_ok("finv", p) != 0 || d1 <= 0 || d2 <= 0) { push_real(s, p); push_real(s, d1); push_real(s, d2); return (d1 <= 0 || d2 <= 0) ? mm_error("finv: degrees of freedom must be positive") : -1; }
  push_real(s, gsl_cdf_fdist_Pinv(p, d1, d2)); return 0;
}
/* k n p binompdf / binomcdf */
static int binom(Stack *s, const char *what, int cdf) {
  double k, n, p; if (three_args(s, what, &k, &n, &p) != 0) return -1;
  if (whole(what, "k", k, 0) != 0 || whole(what, "n", n, 0) != 0 || !(p >= 0 && p <= 1)) {
    push_real(s, k); push_real(s, n); push_real(s, p);
    return (p >= 0 && p <= 1) ? -1 : mm_error("%s: p must be in [0, 1]", what);
  }
  push_real(s, cdf ? gsl_cdf_binomial_P((unsigned)k, p, (unsigned)n) : gsl_ran_binomial_pdf((unsigned)k, p, (unsigned)n));
  return 0;
}
static int op_binompdf(Stack *s) { return binom(s, "binompdf", 0); }
static int op_binomcdf(Stack *s) { return binom(s, "binomcdf", 1); }
/* k mu poispdf / poiscdf */
static int pois(Stack *s, const char *what, int cdf) {
  double k, mu; if (two_args(s, what, &k, &mu) != 0) return -1;
  if (whole(what, "k", k, 0) != 0 || mu <= 0) { push_real(s, k); push_real(s, mu); return mu <= 0 ? mm_error("%s: the mean must be positive", what) : -1; }
  push_real(s, cdf ? gsl_cdf_poisson_P((unsigned)k, mu) : gsl_ran_poisson_pdf((unsigned)k, mu));
  return 0;
}
static int op_poispdf(Stack *s) { return pois(s, "poispdf", 0); }
static int op_poiscdf(Stack *s) { return pois(s, "poiscdf", 1); }

static const Op table[] = {
  { "mean",     1, op_mean,     "Statistics", "v -- m", "mean of a vector (a real; cmean gives column means as a row)", "[1 5 $ 2 4 4 4 6] mean" },
  { "sd",       1, op_sd,       "Statistics", "v -- s", "sample standard deviation (n-1) of a vector", "[1 5 $ 2 4 4 4 6] sd" },
  { "sem",      1, op_sem,      "Statistics", "v -- se", "standard error of the mean, sd/sqrt(n)", "[1 5 $ 2 4 4 4 6] sem" },
  { "median",   1, op_median,   "Statistics", "v -- m", "median", "[1 5 $ 5 1 4 2 3] median" },
  { "quantile", 2, op_quantile, "Statistics", "v p -- q", "the p-quantile (linear interpolation between order statistics)", "[1 5 $ 5 1 4 2 3] 0.9 quantile" },
  { "skew",     1, op_skew,     "Statistics", "v -- g", "skewness", "[1 5 $ 1 2 3 4 10] skew" },
  { "kurt",     1, op_kurt,     "Statistics", "v -- k", "excess kurtosis", "[1 5 $ 1 2 3 4 10] kurt" },
  { "cov",      2, op_cov,      "Statistics", "x y -- c", "sample covariance of two vectors", "[1 4 $ 1 2 3 4] [1 4 $ 2 4 5 9] cov" },
  { "corr",     2, op_corr,     "Statistics", "x y -- r", "Pearson correlation of two vectors", "[1 4 $ 1 2 3 4] [1 4 $ 2 4 5 9] corr" },
  { "movavg",   2, op_movavg,   "Statistics", "v n -- v2", "trailing moving average over n values (partial means at the start)", "[1 6 $ 1 2 3 4 5 6] 3 movavg" },
  { "hist",     2, op_hist,     "Statistics", "v k -- M", "histogram with k equal bins between min and max: bin centre, count", "[1 8 $ 1 2 2 3 3 3 4 9] 4 hist" },
  { "linfit",   2, op_linfit,   "Curve fitting", "x y -- [a,b,r2]", "least-squares line y = a + b x and R²", "[1 4 $ 1 2 3 4] [1 4 $ 2 4 5 9] linfit" },
  { "expfit",   2, op_expfit,   "Curve fitting", "x y -- [a,b,r2]", "exponential fit y = a e^(b x) (least squares on ln y), y > 0", "[1 4 $ 1 2 3 4] [1 4 $ 2.7 7.4 20 55] expfit" },
  { "logfit",   2, op_logfit,   "Curve fitting", "x y -- [a,b,r2]", "logarithmic fit y = a + b ln x, x > 0", "[1 4 $ 1 2 3 4] [1 4 $ 1 1.7 2.1 2.4] logfit" },
  { "powfit",   2, op_powfit,   "Curve fitting", "x y -- [a,b,r2]", "power fit y = a x^b (least squares on logs), x, y > 0", "[1 4 $ 1 2 3 4] [1 4 $ 2 8 18 32] powfit" },
  { "ols",      2, op_ols,      "Curve fitting", "X y -- beta", "multiple regression of y on the columns of X with an intercept added: beta as a column (intercept first)", "[4 1 $ 1 2 3 4] [4 1 $ 2 4 5 9] ols" },
  { "olstab",   2, op_olstab,   "Curve fitting", "X y -- M r2", "the regression table: coefficient, standard error, t, two-sided p per row (intercept first), and R²", "[4 1 $ 1 2 3 4] [4 1 $ 2 4 5 9] olstab" },
  { "tcdf",     2, op_tcdf,     "Distributions", "x df -- p", "Student t cumulative distribution", "2 10 tcdf" },
  { "tinv",     2, op_tinv,     "Distributions", "p df -- x", "Student t quantile", "0.975 10 tinv" },
  { "chi2cdf",  2, op_chi2cdf,  "Distributions", "x df -- p", "chi-square cumulative distribution", "3.84 1 chi2cdf" },
  { "chi2inv",  2, op_chi2inv,  "Distributions", "p df -- x", "chi-square quantile", "0.95 1 chi2inv" },
  { "fcdf",     3, op_fcdf,     "Distributions", "x df1 df2 -- p", "F cumulative distribution", "4 2 10 fcdf" },
  { "finv",     3, op_finv,     "Distributions", "p df1 df2 -- x", "F quantile", "0.95 2 10 finv" },
  { "binompdf", 3, op_binompdf, "Distributions", "k n p -- P", "binomial probability of k successes in n trials", "3 10 0.5 binompdf" },
  { "binomcdf", 3, op_binomcdf, "Distributions", "k n p -- P", "binomial probability of at most k successes", "3 10 0.5 binomcdf" },
  { "poispdf",  2, op_poispdf,  "Distributions", "k mu -- P", "Poisson probability of k events with mean mu", "2 3 poispdf" },
  { "poiscdf",  2, op_poiscdf,  "Distributions", "k mu -- P", "Poisson probability of at most k events", "2 3 poiscdf" },
};

const Module mm41_mod_stat = {
  "stat", "1.0", "descriptive statistics, curve fitting, regression, t/chi-square/F/binomial/Poisson distributions",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
