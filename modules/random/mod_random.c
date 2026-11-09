/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The random module: draws from named distributions, resampling, and
 * Monte Carlo over a word. Scalar draws (rnorm runif rpois rexp) are for
 * words that simulate one outcome; the vector forms (normrnd ...) give n
 * draws as a column. `n @f sims` runs a word n times and collects what it
 * leaves; `mcstats` summarises a vector. All draws come from the
 * calculator's generator (Mersenne twister), which `seed` makes
 * reproducible. */

#include "module.h"
#include "errors.h"
#include "globals.h"
#include "ops.h"
#include "stack.h"
#include "xt.h"
#include <gsl/gsl_randist.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_sort.h>
#include <gsl/gsl_statistics.h>
#include <gsl/gsl_matrix.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int push_col(Stack *s, const double *v, size_t n) {
  gsl_matrix *m = gsl_matrix_alloc(n, 1);
  if (!m) return mm_error("out of memory");
  for (size_t i = 0; i < n; i++) gsl_matrix_set(m, i, 0, v[i]);
  push_matrix_real(s, m);
  return 0;
}

static int op_seed(Stack *s) {
  double v;
  if (pop_real_arg(s, "seed", &v) != 0) return -1;
  if (v != floor(v) || v < 0) { push_real(s, v); return mm_error("seed: needs a whole number >= 0"); }
  gsl_rng_set(global_rng, (unsigned long)v);
  return 0;
}

/* ---- scalar draws ---- */

static int op_rnorm(Stack *s) {
  double mu, sigma;
  if (pop_two_reals(s, "rnorm", &mu, &sigma) != 0) return -1;
  if (sigma < 0) { push_real(s, mu); push_real(s, sigma); return mm_error("rnorm: sigma must be >= 0"); }
  push_real(s, mu + gsl_ran_gaussian(global_rng, sigma));
  return 0;
}
static int op_runif(Stack *s) {
  double a, b;
  if (pop_two_reals(s, "runif", &a, &b) != 0) return -1;
  push_real(s, a + (b - a) * gsl_rng_uniform(global_rng));
  return 0;
}
static int op_rpois(Stack *s) {
  double mu;
  if (pop_real_arg(s, "rpois", &mu) != 0) return -1;
  if (mu <= 0) { push_real(s, mu); return mm_error("rpois: the mean must be positive"); }
  push_real(s, (double)gsl_ran_poisson(global_rng, mu));
  return 0;
}
static int op_rexp(Stack *s) {
  double lambda;
  if (pop_real_arg(s, "rexp", &lambda) != 0) return -1;
  if (lambda <= 0) { push_real(s, lambda); return mm_error("rexp: the rate must be positive"); }
  push_real(s, gsl_ran_exponential(global_rng, 1.0 / lambda));
  return 0;
}

/* ---- vector draws: n params... -- v ---- */

/* Pop the parameters (above the count), then the count. */
static int draw(Stack *s, const char *what, int nparams, double *p, double *n) {
  for (int d = 0; d < nparams; d++) {
    stack_coerce_real(s, d);
    if (s->items[s->top - d].type != TYPE_REAL) return mm_error("%s: needs the count and %d parameter%s", what, nparams, nparams == 1 ? "" : "s");
  }
  stack_coerce_real(s, nparams);
  if (s->items[s->top - nparams].type != TYPE_REAL) return mm_error("%s: needs the count first", what);
  for (int d = 0; d < nparams; d++) p[nparams - 1 - d] = s->items[s->top - d].real;
  *n = s->items[s->top - nparams].real;
  if (*n != floor(*n) || *n < 1 || *n > 50000000) return mm_error("%s: the count must be a whole number from 1 to 50 million", what);
  for (int d = 0; d <= nparams; d++) pop(s);
  return 0;
}

#define VDRAW(fname, what, k, check, expr)                                   \
  static int fname(Stack *s) {                                               \
    double p[3] = {0, 0, 0}, n;                                              \
    if (draw(s, what, k, p, &n) != 0) return -1;                             \
    if (!(check)) { for (int d = 0; d < k; d++) push_real(s, p[d]); push_real(s, n); \
                    return mm_error("%s: invalid parameters", what); }       \
    size_t N = (size_t)n;                                                    \
    double *v = malloc(N * sizeof *v);                                       \
    if (!v) return mm_error("%s: out of memory", what);                      \
    for (size_t i = 0; i < N; i++) v[i] = (expr);                            \
    int rc = push_col(s, v, N);                                              \
    free(v);                                                                 \
    return rc;                                                               \
  }

VDRAW(op_normrnd,  "normrnd",  2, p[1] >= 0,                        p[0] + gsl_ran_gaussian(global_rng, p[1]))
VDRAW(op_unifrnd,  "unifrnd",  2, 1,                                p[0] + (p[1] - p[0]) * gsl_rng_uniform(global_rng))
VDRAW(op_poisrnd,  "poisrnd",  1, p[0] > 0,                         (double)gsl_ran_poisson(global_rng, p[0]))
VDRAW(op_binomrnd, "binomrnd", 2, p[0] >= 0 && p[0] == floor(p[0]) && p[1] >= 0 && p[1] <= 1, (double)gsl_ran_binomial(global_rng, p[1], (unsigned)p[0]))
VDRAW(op_exprnd,   "exprnd",   1, p[0] > 0,                         gsl_ran_exponential(global_rng, 1.0 / p[0]))
VDRAW(op_trnd,     "trnd",     1, p[0] > 0,                         gsl_ran_tdist(global_rng, p[0]))
VDRAW(op_chi2rnd,  "chi2rnd",  1, p[0] > 0,                         gsl_ran_chisq(global_rng, p[0]))

/* ---- resampling ---- */

static int vector_top(const Stack *s, const char *what, double **out, size_t *n) {
  const stack_element *e = &s->items[s->top];
  if (e->type != TYPE_MATRIX_REAL || (e->matrix_real->size1 != 1 && e->matrix_real->size2 != 1))
    return mm_error("%s: needs a real vector", what);
  const gsl_matrix *m = e->matrix_real;
  size_t N = m->size1 * m->size2;
  double *v = malloc(N * sizeof *v);
  if (!v) return mm_error("%s: out of memory", what);
  for (size_t i = 0; i < N; i++) v[i] = m->size2 == 1 ? gsl_matrix_get(m, i, 0) : gsl_matrix_get(m, 0, i);
  *out = v; *n = N;
  return 0;
}

/* v -- v2 : a bootstrap resample, with replacement, the same size */
static int op_resample(Stack *s) {
  double *v; size_t n;
  if (vector_top(s, "resample", &v, &n) != 0) return -1;
  double *r = malloc(n * sizeof *r);
  gsl_ran_sample(global_rng, r, n, v, n, sizeof(double));
  free(v);
  stack_element e = pop(s); stack_element_free(&e);
  int rc = push_col(s, r, n);
  free(r);
  return rc;
}

/* v -- v2 : a random permutation */
static int op_shuffle(Stack *s) {
  double *v; size_t n;
  if (vector_top(s, "shuffle", &v, &n) != 0) return -1;
  gsl_ran_shuffle(global_rng, v, n, sizeof(double));
  stack_element e = pop(s); stack_element_free(&e);
  int rc = push_col(s, v, n);
  free(v);
  return rc;
}

/* ---- Monte Carlo: n @f sims -- v ---- */

static int op_sims(Stack *s) {
  double xt, n;
  if (pop_real_arg(s, "sims", &xt) != 0) return -1;
  if (xt_check(xt) != 0) { push_real(s, xt); return -1; }
  if (pop_real_arg(s, "sims", &n) != 0) { push_real(s, xt); return -1; }
  if (n != floor(n) || n < 1 || n > 10000000) { push_real(s, n); push_real(s, xt); return mm_error("sims: the count must be a whole number from 1 to 10 million"); }
  size_t N = (size_t)n;
  double *v = malloc(N * sizeof *v);
  if (!v) return mm_error("sims: out of memory");
  int depth0 = s->top + 1;
  for (size_t i = 0; i < N; i++) {
    long errs = error_count;
    xt_exec(s, xt);
    if (error_count != errs) { free(v); return mm_error("sims: %s failed on run %zu", xt_name(xt), i + 1); }
    int d = s->top + 1 - depth0;
    if (d == 1 && s->items[s->top].type == TYPE_DECIMAL) {
      double x = dec_to_double(&s->items[s->top].dec);
      s->items[s->top].type = TYPE_REAL; s->items[s->top].real = x;
    }
    if (d != 1 || s->items[s->top].type != TYPE_REAL) {
      while (s->top + 1 > depth0) { stack_element e = pop(s); stack_element_free(&e); }
      free(v);
      return mm_error("sims: %s must leave exactly one real each run ( -- x ); run %zu left %d item%s", xt_name(xt), i + 1, d, d == 1 ? "" : "s");
    }
    v[i] = pop(s).real;
  }
  int rc = push_col(s, v, N);
  free(v);
  return rc;
}

/* v -- [mean sd q05 q50 q95] */
static int op_mcstats(Stack *s) {
  double *v; size_t n;
  if (vector_top(s, "mcstats", &v, &n) != 0) return -1;
  if (n < 2) { free(v); return mm_error("mcstats: needs at least 2 values"); }
  double mean = gsl_stats_mean(v, 1, n), sd = gsl_stats_sd(v, 1, n);
  gsl_sort(v, 1, n);
  double out[5] = { mean, sd,
                    gsl_stats_quantile_from_sorted_data(v, 1, n, 0.05),
                    gsl_stats_quantile_from_sorted_data(v, 1, n, 0.50),
                    gsl_stats_quantile_from_sorted_data(v, 1, n, 0.95) };
  free(v);
  stack_element e = pop(s); stack_element_free(&e);
  gsl_matrix *m = gsl_matrix_alloc(1, 5);
  for (int i = 0; i < 5; i++) gsl_matrix_set(m, 0, i, out[i]);
  push_matrix_real(s, m);
  return 0;
}

static const Op table[] = {
  { "seed",     1, op_seed,     "Random", "n --", "seed the random number generator (reproducible draws from here on)", "42 seed" },
  { "rnorm",    2, op_rnorm,    "Random", "mu sigma -- x", "one normal draw", "0 1 rnorm" },
  { "runif",    2, op_runif,    "Random", "a b -- x", "one uniform draw on [a, b)", "0 1 runif" },
  { "rpois",    1, op_rpois,    "Random", "mu -- k", "one Poisson draw", "3 rpois" },
  { "rexp",     1, op_rexp,     "Random", "rate -- x", "one exponential draw with the given rate", "2 rexp" },
  { "normrnd",  3, op_normrnd,  "Random", "n mu sigma -- v", "n normal draws as a column", "1000 0 1 normrnd sd" },
  { "unifrnd",  3, op_unifrnd,  "Random", "n a b -- v", "n uniform draws on [a, b)", "1000 0 1 unifrnd cmean" },
  { "poisrnd",  2, op_poisrnd,  "Random", "n mu -- v", "n Poisson draws", "1000 3 poisrnd mean" },
  { "binomrnd", 3, op_binomrnd, "Random", "n m p -- v", "n binomial draws: m trials, success probability p", "1000 10 0.5 binomrnd mean" },
  { "exprnd",   2, op_exprnd,   "Random", "n rate -- v", "n exponential draws", "1000 2 exprnd mean" },
  { "trnd",     2, op_trnd,     "Random", "n df -- v", "n Student t draws", "1000 5 trnd sd" },
  { "chi2rnd",  2, op_chi2rnd,  "Random", "n df -- v", "n chi-square draws", "1000 3 chi2rnd mean" },
  { "resample", 1, op_resample, "Random", "v -- v2", "a bootstrap resample of a vector (with replacement, same size)", "[1 5 $ 1 2 3 4 5] resample" },
  { "shuffle",  1, op_shuffle,  "Random", "v -- v2", "a random permutation of a vector", "[1 5 $ 1 2 3 4 5] shuffle" },
  { "sims",     2, op_sims,     "Random", "n xt -- v", "run a word n times, collecting the one real it leaves each time ( -- x ) into a column", ": pnl 0 1 rnorm 100 * ; 10000 @pnl sims mcstats" },
  { "mcstats",  1, op_mcstats,  "Random", "v -- [mean,sd,q05,q50,q95]", "summary of simulated values: mean, sd and the 5th, 50th and 95th percentiles", "1000 0 1 normrnd mcstats" },
};

const Module mm41_mod_random = {
  "random", "1.0", "draws from named distributions, resampling and Monte Carlo over a word",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
