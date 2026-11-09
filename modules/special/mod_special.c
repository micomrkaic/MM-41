/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#include "module.h"
#include "errors.h"
#include "stack.h"
#include "spec_fun.h"
#include "sf_more.h"


static int op_npdf(Stack *stack) { (void)stack; (void)(npdf_wrapper(stack)); return 0; }
static int op_ncdf(Stack *stack) { (void)stack; (void)(ncdf_wrapper(stack)); return 0; }
static int op_nquant(Stack *stack) { (void)stack; (void)(nquant_wrapper(stack)); return 0; }
static int op_gamma(Stack *stack) { (void)stack; (void)(gamma_wrapper(stack)); return 0; }
static int op_ln_gamma(Stack *stack) { (void)stack; (void)(ln_gamma_wrapper(stack)); return 0; }
static int op_beta(Stack *stack) { (void)stack; (void)(beta_wrapper(stack)); return 0; }
static int op_ln_beta(Stack *stack) { (void)stack; (void)(ln_beta_wrapper(stack)); return 0; }

static const Op table[] = {
  { "npdf", 1, op_npdf, "Special functions", "x -- phi(x)", "standard normal density", "0 npdf" },
  { "ncdf", 1, op_ncdf, "Special functions", "x -- Phi(x)", "standard normal cumulative distribution", "1.96 ncdf" },
  { "nquant", 1, op_nquant, "Special functions", "p -- x", "standard normal quantile (inverse of ncdf), 0 < p < 1", "0.975 nquant" },
  { "gamma", 1, op_gamma, "Special functions", "x -- Gamma(x)", "gamma function", "5 gamma" },
  { "ln_gamma", 1, op_ln_gamma, "Special functions", "x -- lnΓ(x)", "log gamma function", "10 ln_gamma" },
  { "beta", 2, op_beta, "Special functions", "a b -- B(a,b)", "beta function", "2 3 beta" },
  { "ln_beta", 2, op_ln_beta, "Special functions", "a b -- lnB(a,b)", "log beta function", "2 3 ln_beta" },
  { "erf",     1, op_erf,     "Special functions", "x -- erf(x)", "error function (elementwise on a real matrix)", "1 erf" },
  { "erfc",    1, op_erfc,    "Special functions", "x -- erfc(x)", "complementary error function", "1 erfc" },
  { "gammap",  2, op_gammap,  "Special functions", "a x -- P(a,x)", "regularised lower incomplete gamma P(a,x)", "2 3 gammap" },
  { "gammaq",  2, op_gammaq,  "Special functions", "a x -- Q(a,x)", "regularised upper incomplete gamma Q(a,x) = 1 - P", "2 3 gammaq" },
  { "betainc", 3, op_betainc, "Special functions", "a b x -- I(x;a,b)", "regularised incomplete beta function", "2 3 0.5 betainc" },
  { "digamma", 1, op_digamma, "Special functions", "x -- psi(x)", "digamma function, the derivative of ln_gamma", "1 digamma" },
  { "zeta",    1, op_zeta,    "Special functions", "x -- zeta(x)", "Riemann zeta function (x != 1)", "2 zeta" },
  { "besselj", 2, op_besselj, "Special functions", "x n -- Jn(x)", "Bessel function of the first kind, integer order n", "2.5 0 besselj" },
  { "bessely", 2, op_bessely, "Special functions", "x n -- Yn(x)", "Bessel function of the second kind, x > 0", "2.5 1 bessely" },
  { "besseli", 2, op_besseli, "Special functions", "x n -- In(x)", "modified Bessel function of the first kind", "1 0 besseli" },
  { "besselk", 2, op_besselk, "Special functions", "x n -- Kn(x)", "modified Bessel function of the second kind, x > 0", "1 0 besselk" },
  { "airyai",  1, op_airyai,  "Special functions", "x -- Ai(x)", "Airy function Ai", "0 airyai" },
  { "airybi",  1, op_airybi,  "Special functions", "x -- Bi(x)", "Airy function Bi", "0 airybi" },
  { "ellk",    1, op_ellk,    "Special functions", "k -- K(k)", "complete elliptic integral of the first kind, modulus k (|k| < 1)", "0.5 ellk" },
  { "elle",    1, op_elle,    "Special functions", "k -- E(k)", "complete elliptic integral of the second kind, modulus k", "0.5 elle" },
  { "expint",  1, op_expint,  "Special functions", "x -- E1(x)", "exponential integral E1(x), x != 0", "1 expint" },
  { "lambertw", 1, op_lambertw, "Special functions", "x -- W(x)", "Lambert W function, principal branch (x >= -1/e)", "1 lambertw" },
};

const Module mm41_mod_special = {
  "special", "1.1", "gamma, beta, the normal distribution, error, Bessel, Airy, elliptic, zeta and more",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
