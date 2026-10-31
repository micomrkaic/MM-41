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

#ifndef SPEC_FUN_H
#define SPEC_FUN_H

#include <gsl/gsl_sf_gamma.h>

double gamma_function(double x);
double ln_gamma_function(double x);
double beta_function(double x, double y);
double ln_beta_function(double x, double y);

#include "stack.h"
double standard_normal_pdf(double x);
double standard_normal_cdf(double x);
double standard_normal_quantile(double p);
void npdf_wrapper(Stack *stack);
void ncdf_wrapper(Stack *stack);
void nquant_wrapper(Stack *stack);
void gamma_wrapper(Stack *stack);
void ln_gamma_wrapper(Stack *stack);
void beta_wrapper(Stack *stack);
void ln_beta_wrapper(Stack *stack);
#endif // SPEC_FUN_H
