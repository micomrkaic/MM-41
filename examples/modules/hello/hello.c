/*
 * An example compiled module for MM-41.
 *
 * Build:   make shared-module DIR=examples/modules/hello      (-> hello.so)
 * Use:     "examples/modules/hello/hello.so" load             (or put it in
 *          ~/.config/mm41/modules/ to have it loaded at startup)
 *
 * A module exports one function, mm41_module(), returning a Module whose
 * table has the same shape as the built-in ones. The calculator's own API
 * (stack, errors, helpers) is available through its headers; the symbols
 * resolve against the running mm41, which is linked with -rdynamic.
 */
#include "module.h"
#include "errors.h"
#include "stack.h"
#include <math.h>

/* x -- x^3 */
static int op_cube(Stack *stack) {
  double x;
  if (pop_real_arg(stack, "cube", &x) != 0) return -1;
  push_real(stack, x * x * x);
  return 0;
}

/* y x -- hypot(y,x) */
static int op_hypot(Stack *stack) {
  double y, x;
  if (pop_two_reals(stack, "hyp", &y, &x) != 0) return -1;
  push_real(stack, hypot(y, x));
  return 0;
}

/* -- */
static int op_hello(Stack *stack) {
  (void)stack;
  mm_warn("hello from a shared-object module");
  return 0;
}

static const Op table[] = {
  { "hello", 0, op_hello, "Example", "--",         "print a greeting",            "hello" },
  { "cube",  1, op_cube,  "Example", "x -- x^3",   "cube of a real number",       "3 cube" },
  { "hyp",   2, op_hypot, "Example", "y x -- r",   "hypotenuse of two reals",     "3 4 hyp" },
};

static int hello_init(void) { return 0; }     /* allocate state here if needed */
static void hello_shutdown(void) {}

static const Module mod = {
  "hello", "0.1", "example shared-object module",
  table, (int)(sizeof table / sizeof table[0]),
  hello_init, hello_shutdown,
  MM41_MODULE_API
};

const Module *mm41_module(void) { return &mod; }
