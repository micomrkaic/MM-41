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

#define _POSIX_C_SOURCE 200809L
#include "errors.h"
#include <gsl/gsl_cdf.h>                    // for gsl_cdf_ugaussian_P, gsl_...
#include <gsl/gsl_complex.h>                // for gsl_complex, GSL_REAL
#include <gsl/gsl_complex_math.h>           // for gsl_complex_rect, gsl_com...
#include <gsl/gsl_matrix_complex_double.h>  // for gsl_matrix_complex_calloc
#include <gsl/gsl_matrix_double.h>          // for gsl_matrix_calloc, gsl_ma...
#include <gsl/gsl_randist.h>                // for gsl_ran_gaussian_pdf
#include <stdbool.h>                        // for bool
#include <math.h>                           // for INFINITY
#include <stdio.h>                          // for fprintf, size_t, stderr
#include <string.h>                         // for strcmp
#include "stack.h"                          // for (anonymous struct)::(anon...
#include "stat_fun.h"                       // for matrix_column_means, matr...

/* Reduce each column (axis "col", result is a row vector) or each row
   (axis "row", result is a column vector) with op sum, mean, var, min or
   max. The matrix is consumed. */
void matrix_reduce(Stack* stack, const char* axis, const char* op) {
  if (stack->top < 0) {
    mm_error("Stack underflow: need a matrix to compute reduction.\n");
    return;
  }

  if ((!axis) || (!(strcmp(axis, "row") == 0) && (!(strcmp(axis, "col") == 0)))) {
    mm_error("Invalid axis: must be \"row\" or \"col\".\n");
    return;
  }

  if (!op || (
      strcmp(op, "sum") != 0 &&
      strcmp(op, "mean") != 0 &&
      strcmp(op, "var") != 0 &&
      strcmp(op, "min") != 0 &&
      strcmp(op, "max") != 0)) {
    mm_error("Invalid operation: must be \"sum\", \"mean\", \"var\", \"min\", or \"max\".\n");
    return;
  }

  stack_element* top = &stack->items[stack->top];
  bool compute_rows = strcmp(axis, "row") == 0;
  bool compute_cols = strcmp(axis, "col") == 0;
  bool do_sum  = strcmp(op, "sum") == 0;
  bool do_mean = strcmp(op, "mean") == 0;
  bool do_var  = strcmp(op, "var") == 0;
  bool do_min  = strcmp(op, "min") == 0;
  bool do_max  = strcmp(op, "max") == 0;

  if (top->type == TYPE_MATRIX_REAL) {
    gsl_matrix* mat = top->matrix_real;
    if (!mat) {
      mm_error("Null real matrix.\n");
      return;
    }
    size_t rows = mat->size1;
    size_t cols = mat->size2;
    gsl_matrix* result = NULL;

    if (compute_rows) {
      result = gsl_matrix_calloc(rows, 1);
      for (size_t i = 0; i < rows; ++i) {
        double acc = 0.0, acc_sq = 0.0;
        double extreme = do_max ? -INFINITY : INFINITY;
        for (size_t j = 0; j < cols; ++j) {
          double val = gsl_matrix_get(mat, i, j);
          acc += val;
          acc_sq += val * val;
          if (do_min && val < extreme) extreme = val;
          if (do_max && val > extreme) extreme = val;
        }
        double res = 0.0;
        if (do_sum) res = acc;
        else if (do_mean) res = acc / cols;
        else if (do_var) {
          double mean = acc / cols;
          res = (acc_sq - cols * mean * mean) / (cols - 1);
        } else if (do_min || do_max) {
          res = extreme;
        }
        gsl_matrix_set(result, i, 0, res);
      }
    } else if (compute_cols) {
      result = gsl_matrix_calloc(1, cols);
      for (size_t j = 0; j < cols; ++j) {
        double acc = 0.0, acc_sq = 0.0;
        double extreme = do_max ? -INFINITY : INFINITY;
        for (size_t i = 0; i < rows; ++i) {
          double val = gsl_matrix_get(mat, i, j);
          acc += val;
          acc_sq += val * val;
          if (do_min && val < extreme) extreme = val;
          if (do_max && val > extreme) extreme = val;
        }
        double res = 0.0;
        if (do_sum) res = acc;
        else if (do_mean) res = acc / rows;
        else if (do_var) {
          double mean = acc / rows;
          res = (acc_sq - rows * mean * mean) / (rows - 1);
        } else if (do_min || do_max) {
          res = extreme;
        }
        gsl_matrix_set(result, 0, j, res);
      }
    }

    /* the reduction replaces its argument: A -- v */
    gsl_matrix_free(top->matrix_real);
    top->matrix_real = result;

  } else if (top->type == TYPE_MATRIX_COMPLEX) {
    gsl_matrix_complex* mat = top->matrix_complex;
    if (!mat) {
      mm_error("Null complex matrix.\n");
      return;
    }
    size_t rows = mat->size1;
    size_t cols = mat->size2;
    gsl_matrix_complex* result = NULL;

    if (compute_rows) {
      result = gsl_matrix_complex_calloc(rows, 1);
      for (size_t i = 0; i < rows; ++i) {
        gsl_complex acc = gsl_complex_rect(0.0, 0.0);
        gsl_complex acc_sq = gsl_complex_rect(0.0, 0.0);
        double maxabs = -INFINITY, minabs = INFINITY;
        gsl_complex maxz = gsl_complex_rect(0, 0), minz = gsl_complex_rect(0, 0);

        for (size_t j = 0; j < cols; ++j) {
          gsl_complex val = gsl_matrix_complex_get(mat, i, j);
          acc = gsl_complex_add(acc, val);
          gsl_complex val_sq = gsl_complex_mul(val, gsl_complex_conjugate(val));
          acc_sq = gsl_complex_add(acc_sq, val_sq);

          double absval = gsl_complex_abs(val);
          if (do_max && absval > maxabs) { maxabs = absval; maxz = val; }
          if (do_min && absval < minabs) { minabs = absval; minz = val; }
        }
        gsl_complex res = gsl_complex_rect(0.0, 0.0);
        if (do_sum) res = acc;
        else if (do_mean) res = gsl_complex_div_real(acc, (double)cols);
        else if (do_var) {
          double mean_norm = gsl_complex_abs(acc) / cols;
          double acc_sq_real = GSL_REAL(acc_sq);
          res = gsl_complex_rect((acc_sq_real - cols * mean_norm * mean_norm) / (cols - 1), 0.0);
        } else if (do_max) res = maxz;
        else if (do_min) res = minz;

        gsl_matrix_complex_set(result, i, 0, res);
      }
    } else if (compute_cols) {
      result = gsl_matrix_complex_calloc(1, cols);
      for (size_t j = 0; j < cols; ++j) {
        gsl_complex acc = gsl_complex_rect(0.0, 0.0);
        gsl_complex acc_sq = gsl_complex_rect(0.0, 0.0);
        double maxabs = -INFINITY, minabs = INFINITY;
        gsl_complex maxz = gsl_complex_rect(0, 0), minz = gsl_complex_rect(0, 0);

        for (size_t i = 0; i < rows; ++i) {
          gsl_complex val = gsl_matrix_complex_get(mat, i, j);
          acc = gsl_complex_add(acc, val);
          gsl_complex val_sq = gsl_complex_mul(val, gsl_complex_conjugate(val));
          acc_sq = gsl_complex_add(acc_sq, val_sq);

          double absval = gsl_complex_abs(val);
          if (do_max && absval > maxabs) { maxabs = absval; maxz = val; }
          if (do_min && absval < minabs) { minabs = absval; minz = val; }
        }
        gsl_complex res = gsl_complex_rect(0.0, 0.0);
        if (do_sum) res = acc;
        else if (do_mean) res = gsl_complex_div_real(acc, (double)rows);
        else if (do_var) {
          double mean_norm = gsl_complex_abs(acc) / rows;
          double acc_sq_real = GSL_REAL(acc_sq);
          res = gsl_complex_rect((acc_sq_real - rows * mean_norm * mean_norm) / (rows - 1), 0.0);
        } else if (do_max) res = maxz;
        else if (do_min) res = minz;

        gsl_matrix_complex_set(result, 0, j, res);
      }
    }

    gsl_matrix_complex_free(top->matrix_complex);
    top->matrix_complex = result;

  } else {
    mm_error("Type error: top stack item must be a matrix (real or complex).\n");
  }
}
