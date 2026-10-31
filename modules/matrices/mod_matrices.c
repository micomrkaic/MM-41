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
#include "matrix_fun.h"
#include "linear_algebra.h"
#include "stat_fun.h"
#include "poly_fun.h"
#include "binary_fun.h"


static int op_tran(Stack *stack) { (void)stack; (void)(matrix_transpose(stack)); return 0; }
static int op_tick_tran(Stack *stack) { (void)stack; (void)(matrix_transpose(stack)); return 0; }
static int op_minv(Stack *stack) { (void)stack; (void)(matrix_inverse(stack)); return 0; }
static int op_pinv(Stack *stack) { (void)stack; (void)(matrix_pseudoinverse(stack)); return 0; }
static int op_det(Stack *stack) { (void)stack; (void)(matrix_determinant(stack)); return 0; }
static int op_eig(Stack *stack) { (void)stack; (void)(matrix_eigen_decompose(stack)); return 0; }
static int op_chol(Stack *stack) { (void)stack; (void)(matrix_cholesky(stack)); return 0; }
static int op_svd(Stack *stack) { (void)stack; (void)(matrix_svd(stack)); return 0; }
static int op_kron(Stack *stack) { (void)stack; (void)(kronecker_top_two(stack)); return 0; }
static int op_dim(Stack *stack) { (void)stack; (void)(matrix_dimensions(stack)); return 0; }
static int op_reshape(Stack *stack) { (void)stack; (void)(reshape_matrix(stack)); return 0; }
static int op_get_aij(Stack *stack) { (void)stack; (void)(select_matrix_element(stack)); return 0; }
static int op_set_aij(Stack *stack) { (void)stack; (void)(set_matrix_element(stack)); return 0; }
static int op_split_mat(Stack *stack) { (void)stack; (void)(split_matrix(stack)); return 0; }
static int op_diag(Stack *stack) { (void)stack; (void)(matrix_extract_diagonal(stack)); return 0; }
static int op_to_diag(Stack *stack) { (void)stack; (void)(make_diag_matrix(stack)); return 0; }
static int op_eye(Stack *stack) { (void)stack; (void)(make_unit_matrix(stack)); return 0; }
static int op_ones(Stack *stack) { (void)stack; (void)(make_matrix_of_ones(stack)); return 0; }
static int op_zeroes(Stack *stack) { (void)stack; (void)(make_matrix_of_zeroes(stack)); return 0; }
static int op_rand(Stack *stack) { (void)stack; (void)(make_random_matrix(stack)); return 0; }
static int op_randn(Stack *stack) { (void)stack; (void)(make_gaussian_random_matrix(stack)); return 0; }
static int op_rrange(Stack *stack) { (void)stack; (void)(make_row_range(stack)); return 0; }
static int op_join_v(Stack *stack) { (void)stack; (void)(stack_join_matrix_vertical(stack)); return 0; }
static int op_join_h(Stack *stack) { (void)stack; (void)(stack_join_matrix_horizontal(stack)); return 0; }
static int op_cumsum_r(Stack *stack) { (void)stack; (void)(matrix_cumsum_rows(stack)); return 0; }
static int op_cumsum_c(Stack *stack) { (void)stack; (void)(matrix_cumsum_cols(stack)); return 0; }
static int op_cmean(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "col", "mean")); return 0; }
static int op_rmean(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "row", "mean")); return 0; }
static int op_csum(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "col", "sum")); return 0; }
static int op_rsum(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "row", "sum")); return 0; }
static int op_cvar(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "col", "var")); return 0; }
static int op_rvar(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "row", "var")); return 0; }
static int op_cmin(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "col", "min")); return 0; }
static int op_rmin(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "row", "min")); return 0; }
static int op_cmax(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "col", "max")); return 0; }
static int op_rmax(Stack *stack) { (void)stack; (void)(matrix_reduce(stack, "row", "max")); return 0; }
static int op_roots(Stack *stack) { (void)stack; (void)(poly_roots(stack)); return 0; }
static int op_pval(Stack *stack) { (void)stack; (void)(poly_eval(stack)); return 0; }

static const Op table[] = {
  { "tran", 1, op_tran, "Matrices", "A -- A^T", "transpose", "[2 3 $ 1 2 3 4 5 6] tran" },
  { "'", 1, op_tick_tran, "Matrices", "A -- A'", "transpose (alias of tran)", "[2 3 $ 1 2 3 4 5 6] '" },
  { "minv", 1, op_minv, "Matrices", "A -- A^-1", "inverse of a square real or complex matrix (error if singular)", "[2 2 $ 4 7 2 6] minv" },
  { "pinv", 1, op_pinv, "Matrices", "A -- A^+", "Moore-Penrose pseudoinverse of a square real matrix (SVD)", "[2 2 $ 1 2 2 4] pinv" },
  { "det", 1, op_det, "Matrices", "A -- det(A)", "determinant of a square real or complex matrix", "[2 2 $ 4 7 2 6] det" },
  { "eig", 1, op_eig, "Matrices", "A -- V L", "eigenvectors (columns of V) and eigenvalues (diagonal of L) of a square real matrix; both complex", "[2 2 $ 2 1 1 2] eig" },
  { "chol", 1, op_chol, "Matrices", "A -- L", "Cholesky factor of a symmetric positive definite real matrix, A = L L^T", "[2 2 $ 4 2 2 3] chol" },
  { "svd", 1, op_svd, "Matrices", "A -- U S V", "singular value decomposition of a real matrix, A = U S V^T", "[2 2 $ 4 7 2 6] svd" },
  { "kron", 2, op_kron, "Matrices", "A B -- A(x)B", "Kronecker product", "[2 2 $ 1 2 3 4] [1 2 $ 5 6] kron" },
  { "dim", 1, op_dim, "Matrices", "A -- A rows cols", "push the dimensions (the matrix stays)", "[2 3 $ 1 2 3 4 5 6] dim" },
  { "reshape", 3, op_reshape, "Matrices", "A rows cols -- B", "reshape in row-major order; rows*cols must match", "[2 3 $ 1 2 3 4 5 6] 3 2 reshape" },
  { "get_aij", 3, op_get_aij, "Matrices", "A i j -- A a_ij", "push element (i,j), 0-based (the matrix stays)", "[2 2 $ 1 2 3 4] 0 1 get_aij" },
  { "set_aij", 4, op_set_aij, "Matrices", "A i j x -- A", "set element (i,j), 0-based, in place; a complex x promotes a real matrix", "[2 2 $ 1 2 3 4] 0 1 9 set_aij" },
  { "split_mat", 1, op_split_mat, "Matrices", "A -- a11 a12 ...", "push all elements as scalars, row by row", "[2 2 $ 1 2 3 4] split_mat" },
  { "diag", 1, op_diag, "Matrices", "A -- v", "main diagonal as a row vector", "[2 2 $ 1 2 3 4] diag" },
  { "to_diag", 1, op_to_diag, "Matrices", "v -- D", "diagonal matrix from a row or column vector", "[1 3 $ 1 2 3] to_diag" },
  { "eye", 1, op_eye, "Matrices", "n -- I", "n x n identity matrix", "3 eye" },
  { "ones", 2, op_ones, "Matrices", "rows cols -- A", "matrix filled with ones", "2 3 ones" },
  { "zeroes", 2, op_zeroes, "Matrices", "rows cols -- A", "matrix filled with zeros", "2 3 zeroes" },
  { "rand", 2, op_rand, "Matrices", "rows cols -- A", "matrix of uniform(0,1) randoms", "2 2 rand" },
  { "randn", 2, op_randn, "Matrices", "rows cols -- A", "matrix of standard normal randoms", "2 2 randn" },
  { "rrange", 3, op_rrange, "Matrices", "start step end -- v", "row vector start, start+step, ... up to end", "0 0.25 1 rrange" },
  { "join_v", 2, op_join_v, "Matrices", "A B -- [A; B]", "stack two matrices vertically (same number of columns)", "[1 2 $ 1 2] [1 2 $ 3 4] join_v" },
  { "join_h", 2, op_join_h, "Matrices", "A B -- [A B]", "join two matrices side by side (same number of rows)", "[2 1 $ 1 2] [2 1 $ 3 4] join_h" },
  { "cumsum_r", 1, op_cumsum_r, "Matrices", "A -- B", "cumulative sum along each row", "[2 2 $ 1 2 3 4] cumsum_r" },
  { "cumsum_c", 1, op_cumsum_c, "Matrices", "A -- B", "cumulative sum down each column", "[2 2 $ 1 2 3 4] cumsum_c" },
  { "cmean", 1, op_cmean, "Matrices", "A -- v", "mean of each column (row vector)", "[2 2 $ 1 2 3 4] cmean" },
  { "rmean", 1, op_rmean, "Matrices", "A -- v", "mean of each row (column vector)", "[2 2 $ 1 2 3 4] rmean" },
  { "csum", 1, op_csum, "Matrices", "A -- v", "sum of each column (row vector)", "[2 2 $ 1 2 3 4] csum" },
  { "rsum", 1, op_rsum, "Matrices", "A -- v", "sum of each row (column vector)", "[2 2 $ 1 2 3 4] rsum" },
  { "cvar", 1, op_cvar, "Matrices", "A -- v", "sample variance of each column (row vector)", "[2 2 $ 1 2 3 4] cvar" },
  { "rvar", 1, op_rvar, "Matrices", "A -- v", "sample variance of each row (column vector)", "[2 2 $ 1 2 3 4] rvar" },
  { "cmin", 1, op_cmin, "Matrices", "A -- v", "minimum of each column", "[2 2 $ 1 2 3 4] cmin" },
  { "rmin", 1, op_rmin, "Matrices", "A -- v", "minimum of each row", "[2 2 $ 1 2 3 4] rmin" },
  { "cmax", 1, op_cmax, "Matrices", "A -- v", "maximum of each column", "[2 2 $ 1 2 3 4] cmax" },
  { "rmax", 1, op_rmax, "Matrices", "A -- v", "maximum of each row", "[2 2 $ 1 2 3 4] rmax" },
  { "roots", 1, op_roots, "Polynomials", "coeffs -- roots", "roots of a real polynomial; coefficients highest degree first, result is a complex row vector", "[1 3 $ 1 -3 2] roots" },
  { "pval", 2, op_pval, "Polynomials", "coeffs x -- y", "evaluate a polynomial (coefficients highest degree first, real or complex) at x", "[1 3 $ 1 0 -1] 2 pval" },
};

const Module mm41_mod_matrices = {
  "matrices", "1.0", "linear algebra, matrix construction and reduction, polynomials",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL
};
