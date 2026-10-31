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

#include "run_machine.h"
#include "globals.h"
#include <stdlib.h>

static int op_run(Stack *stack) {
  char *s;
  if (pop_string_arg(stack, "run", &s) != 0) return 0;
  Program prog = {.count = 0, .label_count = 0};
  if (!load_program_from_file(s, &prog)) {
    mm_error("Failed to load program %s", s);
  } else {
    if (verbose_mode) list_program(&prog);
    run_RPN_code(stack, &prog);
  }
  free_program(&prog);
  free(s);
  return 0;
}



static const Op table[] = {
  { "run", 1, op_run, "Programs", "s --", "load and run the HP-41 style program in file s (LBL/GOTO/GOSUB/RTN/END)", "\"prog1.prg\" run" },
};

const Module mm41_mod_programs = {
  "programs", "1.0", "HP-41 style programs with labels, jumps and subroutines",
  table, (int)(sizeof table / sizeof table[0]), NULL, NULL, 0
};
