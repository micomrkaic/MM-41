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
  { "ln_gamma", 1, op_ln_gamma, "Special functions", "x -- ln Γ(x)", "log gamma function", "10 ln_gamma" },
  { "beta", 2, op_beta, "Special functions", "a b -- B(a,b)", "beta function", "2 3 beta" },
  { "ln_beta", 2, op_ln_beta, "Special functions", "a b -- ln B(a,b)", "log beta function", "2 3 ln_beta" },
};

const Module mm41_mod_special = {
  "special", "1.0", "gamma, beta and the standard normal distribution",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
