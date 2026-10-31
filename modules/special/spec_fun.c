/*
 * This file is part of MM-41
 *
 * MM-41 is free software: 
 * you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MM-41 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MM-41. If not, see <https://www.gnu.org/licenses/>.
 */

#include <gsl/gsl_sf_gamma.h>
#include "spec_fun.h"
#include "math_helpers.h"
#include "unary_fun.h"
#include "stack.h"
#include "errors.h"
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>
#include <stdio.h>

// Computes the Gamma function Γ(x)
double gamma_function(double x) {
  return gsl_sf_gamma(x);
}

double ln_gamma_function(double x) {
  return gsl_sf_lngamma(x);
}

double beta_function(double x, double y) {
    return gsl_sf_beta(x, y);
}

double ln_beta_function(double x, double y) {
    return gsl_sf_lnbeta(x, y);
}

/* ---- standard normal distribution ---- */
double standard_normal_pdf(double x) {
  return gsl_ran_gaussian_pdf(x, 1.0);  // sigma = 1
}

// Wrapper for standard normal cumulative distribution function
double standard_normal_cdf(double x) {
  return gsl_cdf_ugaussian_P(x);  // P = lower tail CDF
}

// Wrapper for standard normal quantile function (inverse CDF)
double standard_normal_quantile(double p) {
  return gsl_cdf_ugaussian_Pinv(p);  // P-inverse = quantile
}


/* ---- stack wrappers ---- */
void npdf_wrapper(Stack *stack) {
  value_type a = stack_top_type(stack);
  if (a == TYPE_REAL) {
    stack_element b = pop(stack);
    push_real(stack, standard_normal_pdf(b.real));
    return;
  }  else if (a == TYPE_COMPLEX) {
    printf("npdf: unsupported type\n");
  } else if (a == TYPE_MATRIX_REAL) {
    apply_real_matrix_unary_inplace(stack, standard_normal_pdf);
    return;
  } else if (a == TYPE_MATRIX_COMPLEX)
    mm_error("npdf: unsupported type\n");
  return;
}

void ncdf_wrapper(Stack *stack) {
  value_type a = stack_top_type(stack);
  if (a == TYPE_REAL) {
    stack_element b = pop(stack);
    push_real(stack, standard_normal_cdf(b.real));
    return;
  }  else if (a == TYPE_COMPLEX) {
    mm_error("ncdf: unsupported type\n");
  } else if (a == TYPE_MATRIX_REAL) {
    apply_real_matrix_unary_inplace(stack, standard_normal_cdf);
    return;
  } else if (a == TYPE_MATRIX_COMPLEX)
    mm_error("ncdf: unsupported type\n");
  return;
}

void nquant_wrapper(Stack *stack) {
  value_type a = stack_top_type(stack);
  if (a == TYPE_REAL) {
    stack_element b = pop(stack);
    push_real(stack, standard_normal_quantile(b.real));
    return;
  }  else if (a == TYPE_COMPLEX) {
    mm_error("nquant: unsupported type\n");
  } else if (a == TYPE_MATRIX_REAL) {
    apply_real_matrix_unary_inplace(stack, standard_normal_quantile);
    return;
  } else if (a == TYPE_MATRIX_COMPLEX)
    mm_error("nquant: unsupported type\n");
  return;
}

void gamma_wrapper(Stack *stack) {
  value_type a = stack_top_type(stack);
  if (a == TYPE_REAL) {
    stack_element b = pop(stack);
    push_real(stack, gamma_function(b.real));
    return;
  }  else if (a == TYPE_COMPLEX) {
    mm_error("nquant: unsupported type\n");
  } else if (a == TYPE_MATRIX_REAL) {
    apply_real_matrix_unary_inplace(stack, gamma_function);
    return;
  } else if (a == TYPE_MATRIX_COMPLEX)
    mm_error("nquant: unsupported type\n");
  return;
}

void ln_gamma_wrapper(Stack *stack) {
  value_type a = stack_top_type(stack);
  if (a == TYPE_REAL) {
    stack_element b = pop(stack);
    push_real(stack, ln_gamma_function(b.real));
    return;
  }  else if (a == TYPE_COMPLEX) {
    mm_error("nquant: unsupported type\n");
  } else if (a == TYPE_MATRIX_REAL) {
    apply_real_matrix_unary_inplace(stack, ln_gamma_function);
    return;
  } else if (a == TYPE_MATRIX_COMPLEX)
    mm_error("nquant: unsupported type\n");
  return;
}

void beta_wrapper(Stack *stack) {
  if (stack_size(stack) < 2) {
    mm_error("Not enough elements on the stack\n");
    return;
  }
  value_type a = stack_top_type(stack);
  value_type b = stack_next2_top_type(stack);
  if ((a == TYPE_REAL) && (b == TYPE_REAL)) {
    stack_element x = pop(stack);
    stack_element y = pop(stack);
    push_real(stack, beta_function(x.real,y.real));
    return;
  }  else {
    mm_error("beta: unsupported type\n");
    return;
  }
}

void ln_beta_wrapper(Stack *stack) {
  if (stack_size(stack) < 2) {
    mm_error("Not enough elements on the stack\n");
    return;
  }
  value_type a = stack_top_type(stack);
  value_type b = stack_next2_top_type(stack);
  if ((a == TYPE_REAL) && (b == TYPE_REAL)) {
    stack_element x = pop(stack);
    stack_element y = pop(stack);
    push_real(stack, ln_beta_function(x.real,y.real));
    return;
  }  else {
    mm_error("ln_beta: unsupported type\n");
    return;
  }
}
