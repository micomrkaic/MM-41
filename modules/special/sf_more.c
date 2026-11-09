/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The rest of the Math Pac: error function, Bessel, Airy, elliptic
   integrals, zeta, digamma, incomplete gamma and beta, exponential
   integral, Lambert W. All through GSL's error-reporting (_e) entry points,
   so a domain or overflow problem is a calculator error, not a nan.
   One-argument functions apply elementwise to a real matrix. */

#include "sf_more.h"
#include "errors.h"
#include "ops.h"
#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf_airy.h>
#include <gsl/gsl_sf_bessel.h>
#include <gsl/gsl_sf_ellint.h>
#include <gsl/gsl_sf_erf.h>
#include <gsl/gsl_sf_expint.h>
#include <gsl/gsl_sf_gamma.h>
#include <gsl/gsl_sf_lambert.h>
#include <gsl/gsl_sf_psi.h>
#include <gsl/gsl_sf_zeta.h>
#include <gsl/gsl_matrix.h>
#include <math.h>

typedef int (*sf1)(double, gsl_sf_result *);

static int sf_check(const char *what, int status, double x) {
  if (status == GSL_SUCCESS) return 0;
  if (status == GSL_EDOM)   return mm_error("%s: %g is outside the domain", what, x);
  if (status == GSL_EOVRFLW) return mm_error("%s: overflow at %g", what, x);
  if (status == GSL_EUNDRFLW) return 0;                  /* a tiny value, fine */
  return mm_error("%s: %s (at %g)", what, gsl_strerror(status), x);
}

/* x -- f(x), elementwise on a real matrix */
static int unary(Stack *s, const char *what, sf1 f) {
  stack_element *e = &s->items[s->top];
  if (e->type == TYPE_MATRIX_REAL) {
    gsl_matrix *m = e->matrix_real;
    for (size_t i = 0; i < m->size1; i++)
      for (size_t j = 0; j < m->size2; j++) {
        gsl_sf_result r;
        double x = gsl_matrix_get(m, i, j);
        if (sf_check(what, f(x, &r), x) != 0) return -1;
        gsl_matrix_set(m, i, j, r.val);
      }
    return 0;
  }
  double x;
  if (pop_real_arg(s, what, &x) != 0) return -1;
  gsl_sf_result r;
  if (sf_check(what, f(x, &r), x) != 0) return -1;
  push_real(s, r.val);
  return 0;
}

static int airy_ai(double x, gsl_sf_result *r) { return gsl_sf_airy_Ai_e(x, GSL_PREC_DOUBLE, r); }
static int airy_bi(double x, gsl_sf_result *r) { return gsl_sf_airy_Bi_e(x, GSL_PREC_DOUBLE, r); }
static int ellk(double k, gsl_sf_result *r) { return gsl_sf_ellint_Kcomp_e(k, GSL_PREC_DOUBLE, r); }
static int elle(double k, gsl_sf_result *r) { return gsl_sf_ellint_Ecomp_e(k, GSL_PREC_DOUBLE, r); }

int op_erf(Stack *s)     { return unary(s, "erf", gsl_sf_erf_e); }
int op_erfc(Stack *s)    { return unary(s, "erfc", gsl_sf_erfc_e); }
int op_airyai(Stack *s)  { return unary(s, "airyai", airy_ai); }
int op_airybi(Stack *s)  { return unary(s, "airybi", airy_bi); }
int op_ellk(Stack *s)    { return unary(s, "ellk", ellk); }
int op_elle(Stack *s)    { return unary(s, "elle", elle); }
int op_zeta(Stack *s)    { return unary(s, "zeta", gsl_sf_zeta_e); }
int op_digamma(Stack *s) { return unary(s, "digamma", gsl_sf_psi_e); }
int op_expint(Stack *s)  { return unary(s, "expint", gsl_sf_expint_E1_e); }
int op_lambertw(Stack *s){ return unary(s, "lambertw", gsl_sf_lambert_W0_e); }

/* x n -- f_n(x): Bessel functions of integer order */
typedef int (*sfn)(int, double, gsl_sf_result *);
static int bessel(Stack *s, const char *what, sfn f) {
  double x, n;
  if (pop_two_reals(s, what, &x, &n) != 0) return -1;
  if (n != floor(n) || fabs(n) > 1000000) return mm_error("%s: the order must be a whole number", what);
  gsl_sf_result r;
  if (sf_check(what, f((int)n, x, &r), x) != 0) return -1;
  push_real(s, r.val);
  return 0;
}
int op_besselj(Stack *s) { return bessel(s, "besselj", gsl_sf_bessel_Jn_e); }
int op_bessely(Stack *s) { return bessel(s, "bessely", gsl_sf_bessel_Yn_e); }
int op_besseli(Stack *s) { return bessel(s, "besseli", gsl_sf_bessel_In_e); }
int op_besselk(Stack *s) { return bessel(s, "besselk", gsl_sf_bessel_Kn_e); }

/* a x -- P(a,x), Q(a,x): regularised incomplete gamma */
static int gammainc(Stack *s, const char *what, int upper) {
  double a, x;
  if (pop_two_reals(s, what, &a, &x) != 0) return -1;
  if (a <= 0 || x < 0) return mm_error("%s: needs a > 0 and x >= 0", what);
  gsl_sf_result r;
  int st = upper ? gsl_sf_gamma_inc_Q_e(a, x, &r) : gsl_sf_gamma_inc_P_e(a, x, &r);
  if (sf_check(what, st, x) != 0) return -1;
  push_real(s, r.val);
  return 0;
}
int op_gammap(Stack *s) { return gammainc(s, "gammap", 0); }
int op_gammaq(Stack *s) { return gammainc(s, "gammaq", 1); }

/* a b x -- I_x(a,b): regularised incomplete beta */
int op_betainc(Stack *s) {
  for (int d = 0; d < 3; d++) {
    stack_coerce_real(s, d);
    if (s->items[s->top - d].type != TYPE_REAL) return mm_error("betainc: needs three real numbers a b x");
  }
  double a = s->items[s->top - 2].real, b = s->items[s->top - 1].real, x = s->items[s->top].real;
  if (a <= 0 || b <= 0 || x < 0 || x > 1) return mm_error("betainc: needs a > 0, b > 0 and 0 <= x <= 1");
  gsl_sf_result r;
  if (sf_check("betainc", gsl_sf_beta_inc_e(a, b, x, &r), x) != 0) return -1;
  pop(s); pop(s); pop(s);
  push_real(s, r.val);
  return 0;
}
