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
#include "registers.h"
#include "globals.h"

static int registers_init(void) { init_registers(); return 0; }
static void registers_shutdown(void) { free_all_registers(); }

static int op_sto(Stack *stack) { (void)stack; (void)(store_to_register(stack)); return 0; }
static int op_rcl(Stack *stack) { (void)stack; (void)(recall_from_register(stack)); return 0; }
static int op_ffr(Stack *stack) { (void)stack; (void)(find_first_free_register(stack)); return 0; }
static int op_pr(Stack *stack) { (void)stack; (void)(show_registers_status()); return 0; }
static int op_saveregs(Stack *stack) { (void)stack; (void)(save_registers_to_file(REGISTERS_PATH)); return 0; }
static int op_loadregs(Stack *stack) { (void)stack; (void)(load_registers_from_file(REGISTERS_PATH)); return 0; }
static int op_clregs(Stack *stack) { (void)stack; (void)(free_all_registers()); return 0; }

static const Op table[] = {
  { "sto", 2, op_sto, "Registers", "x n --", "store x in register n (0-63); any type", "42 0 sto" },
  { "rcl", 1, op_rcl, "Registers", "n -- x", "push a copy of register n", "0 rcl" },
  { "ffr", 0, op_ffr, "Registers", "-- n", "index of the first free register", "ffr" },
  { "pr", 0, op_pr, "Registers", "--", "show which registers are occupied", "pr" },
  { "saveregs", 0, op_saveregs, "Registers", "--", "save all registers to registers.txt in the config directory", "saveregs" },
  { "loadregs", 0, op_loadregs, "Registers", "--", "load the registers saved by saveregs", "loadregs" },
  { "clregs", 0, op_clregs, "Registers", "--", "clear all registers", "clregs" },
};

const Module mm41_mod_registers = {
  "registers", "1.0", "64 storage registers of any type",
  table, (int)(sizeof table / sizeof table[0]), registers_init, registers_shutdown, 0
};
