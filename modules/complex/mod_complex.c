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

#include "math_helpers.h"
#include "binary_fun.h"
#include "unary_fun.h"
#include <gsl/gsl_complex_math.h>

static gsl_complex conj_c(gsl_complex z) { return gsl_complex_conjugate(z); }
static double conj_r(double x) { return x; }

static void conj_wrapper(Stack *stack) {
  switch (stack_top_type(stack)) {
  case TYPE_REAL: return;
  case TYPE_COMPLEX: apply_complex_unary(stack, conj_c); return;
  case TYPE_MATRIX_REAL: apply_real_matrix_unary_inplace(stack, conj_r); return;
  case TYPE_MATRIX_COMPLEX: apply_complex_matrix_unary_inplace(stack, conj_c); return;
  default: mm_error("conj: unsupported type");
  }
}

static int op_re(Stack *stack) { (void)stack; (void)(re_wrapper(stack)); return 0; }
static int op_im(Stack *stack) { (void)stack; (void)(im_wrapper(stack)); return 0; }
static int op_arg(Stack *stack) { (void)stack; (void)(arg_wrapper(stack)); return 0; }
static int op_conj(Stack *stack) { (void)stack; (void)(conj_wrapper(stack)); return 0; }
static int op_re2c(Stack *stack) { (void)stack; (void)(real2complex(stack)); return 0; }
static int op_split_c(Stack *stack) { (void)stack; (void)(split_complex(stack)); return 0; }
static int op_j2r(Stack *stack) { (void)stack; (void)(join_2_reals(stack)); return 0; }

static const Op table[] = {
  { "re", 1, op_re, "Complex", "z -- Re(z)", "real part (a real is unchanged)", "(1,2) re" },
  { "im", 1, op_im, "Complex", "z -- Im(z)", "imaginary part (0 for a real)", "(1,2) im" },
  { "arg", 1, op_arg, "Complex", "z -- arg(z)", "argument (phase) in radians, in (-pi, pi]", "(0,1) arg" },
  { "conj", 1, op_conj, "Complex", "z -- conj(z)", "complex conjugate (elementwise for matrices; identity for reals)", "(1,2) conj" },
  { "re2c", 1, op_re2c, "Complex", "x -- (x,0)", "promote a real to complex", "5 re2c" },
  { "split_c", 1, op_split_c, "Complex", "z -- Re(z) Im(z)", "split a complex number into real and imaginary parts", "(1,2) split_c" },
  { "j2r", 2, op_j2r, "Complex", "re im -- (re,im)", "join two reals into a complex number (imaginary part on top)", "1 2 j2r" },
};

const Module mm41_mod_complex = {
  "complex", "1.0", "complex numbers: parts, argument, conjugate, construction",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL
};
