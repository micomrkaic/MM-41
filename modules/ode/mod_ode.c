/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The ode module: initial-value problems y' = f(t, y) with an adaptive
 * Runge-Kutta-Fehlberg (4,5) driver from GSL, the Math Pac's differential
 * equation solver with a word as the right-hand side.
 *
 * The word takes t and y and leaves y':   : f swap drop chs ;   is y' = -y.
 * A system is the same with y a row vector: the word gets t and a 1 x k
 * matrix and must leave a vector of k. `odeint` returns the trajectory at
 * n+1 equally spaced times as a matrix (t in the first column), `odesol`
 * only the final state. The tolerance is `tol`, shared with the numerical
 * methods. */

#include "module.h"
#include "errors.h"
#include "globals.h"
#include "ops.h"
#include "stack.h"
#include "xt.h"
#include <gsl/gsl_errno.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_odeiv2.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Stack *s; double xt; size_t k; long evals; const char *what; } Rhs;

#define MAX_RHS_EVALS 2000000

/* GSL callback: push t and y, run the word, pop y'. */
static int rhs(double t, const double y[], double dydt[], void *params) {
  Rhs *r = params;
  Stack *s = r->s;
  if (++r->evals > MAX_RHS_EVALS) { mm_error("%s: more than %d evaluations; loosen tol", r->what, MAX_RHS_EVALS); return GSL_EBADFUNC; }
  int depth0 = s->top + 1;
  long errs = error_count;
  push_real(s, t);
  if (r->k == 1) push_real(s, y[0]);
  else {
    gsl_matrix *m = gsl_matrix_alloc(1, r->k);
    for (size_t j = 0; j < r->k; j++) gsl_matrix_set(m, 0, j, y[j]);
    push_matrix_real(s, m);
  }
  xt_exec(s, r->xt);
  if (error_count != errs) { mm_error("  while evaluating %s at t = %.17g", xt_name(r->xt), t); return GSL_EBADFUNC; }
  int d = s->top + 1 - depth0;
  int ok = 0;
  if (d == 1) {
    stack_element *e = &s->items[s->top];
    if (e->type == TYPE_DECIMAL) { double v = dec_to_double(&e->dec); e->type = TYPE_REAL; e->real = v; }
    if (r->k == 1 && e->type == TYPE_REAL) { dydt[0] = e->real; ok = 1; }
    else if (e->type == TYPE_MATRIX_REAL && e->matrix_real->size1 * e->matrix_real->size2 == r->k) {
      const gsl_matrix *m = e->matrix_real;
      for (size_t j = 0; j < r->k; j++) dydt[j] = m->size2 == 1 ? gsl_matrix_get(m, j, 0) : gsl_matrix_get(m, 0, j);
      ok = 1;
    }
  }
  while (s->top + 1 > depth0) { stack_element e = pop(s); stack_element_free(&e); }
  if (!ok) {
    mm_error("%s must leave y' as %s (t y -- y'); it left %d item%s", xt_name(r->xt),
             r->k == 1 ? "one real" : "a vector of the same length as y", d, d == 1 ? "" : "s");
    return GSL_EBADFUNC;
  }
  for (size_t j = 0; j < r->k; j++)
    if (!isfinite(dydt[j])) { mm_error("%s: y' is not finite at t = %.17g", xt_name(r->xt), t); return GSL_EBADFUNC; }
  return GSL_SUCCESS;
}

/* Read y0 (real or vector) at depth; returns k and fills y (malloc'd). */
static int read_y0(const Stack *s, int depth, const char *what, double **y, size_t *k) {
  const stack_element *e = &s->items[s->top - depth];
  if (e->type == TYPE_REAL || e->type == TYPE_DECIMAL) {
    *y = malloc(sizeof **y);
    (*y)[0] = e->type == TYPE_REAL ? e->real : dec_to_double(&e->dec);
    *k = 1;
    return 0;
  }
  if (e->type == TYPE_MATRIX_REAL && (e->matrix_real->size1 == 1 || e->matrix_real->size2 == 1)) {
    const gsl_matrix *m = e->matrix_real;
    *k = m->size1 * m->size2;
    *y = malloc(*k * sizeof **y);
    for (size_t j = 0; j < *k; j++) (*y)[j] = m->size2 == 1 ? gsl_matrix_get(m, j, 0) : gsl_matrix_get(m, 0, j);
    return 0;
  }
  return mm_error("%s: y0 must be a real or a vector", what);
}

/* Integrate from t0 to t1 with `steps` output intervals; out is (steps+1) x (k+1) or NULL for the final state only. */
static int integrate(Stack *s, const char *what, double xt, double *y, size_t k, double t0, double t1, long steps, gsl_matrix **out) {
  Rhs r = { s, xt, k, 0, what };
  gsl_odeiv2_system sys = { rhs, NULL, k, &r };
  double tol = num_tol > 0 ? num_tol : 1e-10;
  double h = (t1 - t0) / 100.0;
  if (h == 0) h = 1e-6;
  gsl_odeiv2_driver *d = gsl_odeiv2_driver_alloc_y_new(&sys, gsl_odeiv2_step_rkf45, h, tol, tol);
  if (!d) return mm_error("%s: out of memory", what);
  gsl_matrix *m = NULL;
  if (out) {
    m = gsl_matrix_alloc((size_t)steps + 1, k + 1);
    gsl_matrix_set(m, 0, 0, t0);
    for (size_t j = 0; j < k; j++) gsl_matrix_set(m, 0, j + 1, y[j]);
  }
  double t = t0;
  for (long i = 1; i <= steps; i++) {
    double ti = t0 + (t1 - t0) * (double)i / (double)steps;
    int st = gsl_odeiv2_driver_apply(d, &t, ti, y);
    if (st != GSL_SUCCESS) {
      gsl_odeiv2_driver_free(d);
      if (m) gsl_matrix_free(m);
      if (st == GSL_EBADFUNC) return -1;                     /* the word already reported */
      return mm_error("%s: the integrator failed at t = %g (%s); the solution may blow up before t1, or tol is too tight", what, t, gsl_strerror(st));
    }
    if (m) {
      gsl_matrix_set(m, (size_t)i, 0, t);
      for (size_t j = 0; j < k; j++) gsl_matrix_set(m, (size_t)i, j + 1, y[j]);
    }
  }
  gsl_odeiv2_driver_free(d);
  num_evals = r.evals;
  if (out) *out = m;
  return 0;
}

/* y0 t0 t1 n xt -- M */
static int op_odeint(Stack *s) {
  double xt, n, t0, t1;
  if (pop_real_arg(s, "odeint", &xt) != 0) return -1;
  if (xt_check(xt) != 0) return -1;
  if (pop_real_arg(s, "odeint", &n) != 0) return -1;
  if (n != floor(n) || n < 1 || n > 10000000) return mm_error("odeint: the number of steps must be a whole number >= 1");
  if (pop_two_reals(s, "odeint", &t0, &t1) != 0) return -1;
  if (!isfinite(t0) || !isfinite(t1) || t0 == t1) return mm_error("odeint: t0 and t1 must be finite and different");
  double *y; size_t k;
  if (read_y0(s, 0, "odeint", &y, &k) != 0) return -1;
  gsl_matrix *m;
  int rc = integrate(s, "odeint", xt, y, k, t0, t1, (long)n, &m);
  free(y);
  if (rc != 0) return -1;
  stack_element e = pop(s); stack_element_free(&e);
  push_matrix_real(s, m);
  return 0;
}

/* y0 t0 t1 xt -- y */
static int op_odesol(Stack *s) {
  double xt, t0, t1;
  if (pop_real_arg(s, "odesol", &xt) != 0) return -1;
  if (xt_check(xt) != 0) return -1;
  if (pop_two_reals(s, "odesol", &t0, &t1) != 0) return -1;
  if (!isfinite(t0) || !isfinite(t1) || t0 == t1) return mm_error("odesol: t0 and t1 must be finite and different");
  double *y; size_t k;
  if (read_y0(s, 0, "odesol", &y, &k) != 0) return -1;
  int rc = integrate(s, "odesol", xt, y, k, t0, t1, 1, NULL);
  if (rc != 0) { free(y); return -1; }
  stack_element e = pop(s); stack_element_free(&e);
  if (k == 1) push_real(s, y[0]);
  else {
    gsl_matrix *m = gsl_matrix_alloc(1, k);
    for (size_t j = 0; j < k; j++) gsl_matrix_set(m, 0, j, y[j]);
    push_matrix_real(s, m);
  }
  free(y);
  return 0;
}

static const Op table[] = {
  { "odeint", 5, op_odeint, "Differential equations", "y0 t0 t1 n xt -- M",
    "integrate y' = f(t,y) from t0 to t1 (adaptive RKF45, tolerance tol); the word is t y -- y'; M has n+1 rows of t, y (y a real, or a row vector for a system)",
    ": f swap drop chs ; 1 0 2 4 @f odeint pm" },
  { "odesol", 4, op_odesol, "Differential equations", "y0 t0 t1 xt -- y",
    "the state at t1 only", ": f swap drop chs ; 1 0 1 @f odesol" },
};

const Module mm41_mod_ode = {
  "ode", "1.0", "initial-value problems y' = f(t, y) over a word, scalar or system (RKF45)",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
