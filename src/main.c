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
 * along with MM-41. If not, see
 * <https://www.gnu.org/licenses/>.
 */

/* TODO as of January 18, 2026
   . use pop() consistently instead of accessing stack internally through the pointer
   . make sure strings do not leak after date operations
   . fix the date format in my_date.c to throw error on e.g., "33.14.2026"
   . physical constants
   . call from emacs and shell as a calculator/evaluator
   . implement in JSON save_stack_to_file in the interpreter and
   load_stack_from_file . Simple plotting of a vector vs vector for a simple
   function plot etc. . select submatrices; resize matrices and add/remove rows
   and/or columns . clean up and consolidate binary_fun.c; cleanup the dispatch
   table . clean up the interpreter to have only one dispatch table in the VM .
   Overlay for registers -- store variables; recall values with <- . load
   program, list program, run program -> separate instructions . implement loop
   counters and easier comparison registers for iterations; . fully implement
   counters and tests . sto ind, rcl ind. . Test full HP-41 style programming
   with GTO, RTN, XEQ, ISG, DSE, LBL etc. and labels . Automatic cleanup of
   matrices and strings with __cleanup__
*/

#define _POSIX_C_SOURCE 200809L

#include "errors.h"
#include "eval_fun.h"          // for evaluate_line
#include "skins.h"
#include "eggs.h"
#include "help.h"
#include "manual.h"              // for help_topic
#include "tutor.h"
#include "ops.h"               // for print_ops_markdown
#include "globals.h"           // for CONFIG_PATH, HISTORY_PATH, completed_...
#include "print_fun.h"         // for print_stack
#include "module.h"
#include "decimal.h"
#include "splash.h"            // for splash_screen
#include "stack.h"             // for copy_stack, free_stack, init_stack
#include "tab_completion.h"    // for function_name_completion
#include "words.h"             // for list_macros, load_macros_from_file
#include <ctype.h>             // for isspace
#include <errno.h>             // for errno
#include <gsl/gsl_errno.h>     // for gsl_set_error_handler
#include <gsl/gsl_rng.h>       // for gsl_rng_alloc, gsl_rng, gsl_rng_mt19937
#include <readline/history.h>  // for add_history, read_history, stifle_his...
#include <readline/readline.h> // for readline, rl_attempted_completion_fun...
#include <stdbool.h>           // for false
#include <stdio.h>             // for fprintf, perror, stderr, NULL
#include <stdlib.h>            // for free, WEXITSTATUS, system, WIFEXITED
#include <string.h>            // for strcmp
#include <unistd.h>            // for isatty

// Globals
gsl_rng
    *global_rng; // Global random number generator, used throughout the program


// Local declarations
int repl(void);

static void quiet_error_handler(const char *reason, const char *file, int line, int gsl_errno) {
  (void)reason; (void)file; (void)line;
  if (gsl_errno == GSL_ENOMEM && mm_nomem_escape) mm_out_of_memory();
}

mm_gsl_handler *mm_gsl_errors_off(void) { return gsl_set_error_handler(&quiet_error_handler); }

void my_error_handler(const char *reason, const char *file, int line,
                      int gsl_errno) {
  /* out of memory inside a command: abandon the command (see errors.h) */
  if (gsl_errno == GSL_ENOMEM && mm_nomem_escape) mm_out_of_memory();
  mm_error("GSL ERROR: %s (%s:%d) [code=%d]\n", reason, file, line,
          gsl_errno);
  // Do NOT call abort(); this allows graceful recovery
}

/* Read all of cmd's stdout into a malloc'd buffer.
   Returns NULL on failure. Caller owns the returned string. */
static char *capture_command_output(const char *cmd, int *wait_status_out) {
  if (wait_status_out)
    *wait_status_out = 0;

  errno = 0;
  FILE *fp = popen(cmd, "r"); // runs via /bin/sh -c cmd (POSIX)
  if (!fp)
    return NULL;

  size_t cap = 4096;
  size_t len = 0;
  char *buf = malloc(cap);
  if (!buf) {
    pclose(fp);
    return NULL;
  }

  for (;;) {
    if (len + 2048 + 1 > cap) {
      cap *= 2;
      char *tmp = realloc(buf, cap);
      if (!tmp) {
        free(buf);
        pclose(fp);
        return NULL;
      }
      buf = tmp;
    }
    size_t n = fread(buf + len, 1, 2048, fp);
    len += n;
    if (n == 0) {
      if (ferror(fp)) {
        free(buf);
        pclose(fp);
        return NULL;
      }
      break; // EOF
    }
  }

  buf[len] = '\0';
  int st = pclose(fp);
  if (wait_status_out)
    *wait_status_out = st;

  // Trim trailing whitespace/newlines (optional but usually nicer)
  while (len > 0 && isspace((unsigned char)buf[len - 1])) {
    buf[--len] = '\0';
  }
  return buf;
}

bool batch_mode = false;          /* -e, -f, or stdin that is not a terminal */
static bool autoload = true;      /* -n turns off macros/config at startup */

static void load_modules(void) {
  dec_init();
  for (const Module *const *m = mm41_builtin_modules; *m; m++)
    if (modules_register(*m) != 0) exit(2);
  if (modules_init_all() != 0) exit(2);
}

static void startup(Stack *stack, Stack *old_stack) {
  init_stack(stack);
  init_stack(old_stack);
  load_modules();
  if (autoload) {
    autoload_shared_modules();
    autoload_word_modules();
    autoload_words();
    load_config(CONFIG_PATH);
  }
}

static void shutdown(Stack *stack, Stack *old_stack) {
  if (autoload && !batch_mode) save_config(CONFIG_PATH);
  free_stack(old_stack);
  free_stack(stack);
  modules_shutdown_all();
  free_words();
}

/* The words that take a name as a string. At the line level `use const` is
   the same as `"const" use`: a line that is exactly one of these words and
   one bare name is rewritten, so the name is not looked up as a word.
   Anything else -- a quoted name, more tokens, the word inside a definition
   -- is evaluated as written. */
static const char *const bare_name_words[] = { "use", "unuse", "load", "run", "batch", "usehol", "skin", NULL };

static const char *bare_name_form(const char *line, char *out, size_t cap) {
  const char *p = line;
  while (*p == ' ' || *p == '\t') p++;
  for (int i = 0; bare_name_words[i]; i++) {
    size_t wl = strlen(bare_name_words[i]);
    if (strncmp(p, bare_name_words[i], wl) != 0 || (p[wl] != ' ' && p[wl] != '\t')) continue;
    const char *a = p + wl;
    while (*a == ' ' || *a == '\t') a++;
    if (*a == '\0' || *a == '"') return NULL;              /* nothing, or already a string */
    const char *e = a;
    while (*e && *e != ' ' && *e != '\t') e++;
    const char *rest = e;
    while (*rest == ' ' || *rest == '\t') rest++;
    if (*rest != '\0') return NULL;                        /* more than one argument: leave it alone */
    if (memchr(a, '"', (size_t)(e - a))) return NULL;
    if ((size_t)(e - a) + wl + 4 >= cap) return NULL;
    snprintf(out, cap, "\"%.*s\" %s", (int)(e - a), a, bare_name_words[i]);
    return out;
  }
  return NULL;
}

/* One line of input, shared by the REPL and batch mode. Returns false on "q". */
static bool handle_line(Stack *stack, Stack *old_stack, char *line) {
  if (tutor_command(line, stack)) return true;   /* in a lesson: skip back again q */
  if (strcmp(line, "q") == 0) return false;

  if (line[0] == '!') {
    int st = 0;
    char *out = capture_command_output(line + 1, &st);
    if (!out) {
      mm_perror("popen/capture_command_output");
    } else {
      push_string(stack, out);
      if (WIFEXITED(st) && WEXITSTATUS(st) != 0)
        mm_error("Command exited with status %d", WEXITSTATUS(st));
      else if (!WIFEXITED(st))
        mm_error("Command terminated abnormally");
      free(out);
    }
    return true;
  }

  {                                           /* help <topic>: a line-level form, so the topic is not executed */
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, "help", 4) && (p[4] == ' ' || p[4] == '\t')) { help_topic(p + 4); return true; }
    if (!strncmp(p, "manual", 6) && (p[6] == ' ' || p[6] == '\t')) { manual_open(p + 6); return true; }
    if (!strncmp(p, "tutor", 5) && (p[5] == ' ' || p[5] == '\t' || p[5] == '\0')) { tutor_line(p + 5, stack); return true; }
    if (!strncmp(p, "examples", 8) && (p[8] == ' ' || p[8] == '\t' || p[8] == '\0')) { examples_line(p + 8); return true; }
  }

  if (egg_line(line)) return true;

  char rewritten[1024];
  const char *bare = bare_name_form(line, rewritten, sizeof rewritten);

  static bool can_undo = true;                 /* false when there was no memory to keep the stack as it was */
  if (!strcmp(line, "undo")) {
    if (can_undo) copy_stack(stack, old_stack);
    else mm_error("undo: the stack was not saved before the last line (out of memory)");
  } else {
    can_undo = snapshot_stack(old_stack, stack); // Preserve the stack
    long errs = error_count;
    evaluate_line(stack, bare ? rewritten : line);
    if (bare && can_undo && error_count != errs) copy_stack(stack, old_stack);   /* a failed `use x` leaves no "x" behind */
    if (stop_on_error && error_count != errs) return false;          /* -x: the first error ends the run */
  }
  if (exit_status >= 0) return false;                                /* n exit */
  return true;
}

/* For --tutor-check: a line through the REPL's handler, with a throwaway undo stack. */
static bool check_line(Stack *stack, char *line) {
  static Stack old; static bool ready;
  if (!ready) { init_stack(&old); ready = true; }
  return handle_line(stack, &old, line);
}

int repl(void) {
  Stack stack, old_stack;
  splash_screen();
  startup(&stack, &old_stack);
  if (verbose_mode) list_macros();
  read_history(HISTORY_PATH);
  stifle_history(1000); // Keep only the last 1000 commands
  rl_attempted_completion_function = function_name_completion;

  while (1) {
    const char *tp = tutor_prompt();
    char *line = readline(tp ? tp : decimal_mode ? "MM-41[dec]>> " : "MM-41>> ");
    if (!line) { putchar('\n'); break; }
    if (*line) add_history(line);
    bool go_on = handle_line(&stack, &old_stack, line);
    free(line);
    if (!go_on) break;
    if (completed_batch)
      completed_batch = false;
    else if (!skip_stack_printing)
      print_stack_display(&stack);
    skip_stack_printing = false;
    tutor_after(&stack);
  }

  write_history(HISTORY_PATH);
  shutdown(&stack, &old_stack);
  return 0;
}

/* Batch mode: no splash, no echo of the stack after each line. The lines
   from -e/-f/stdin run in order; the final stack is printed once at the
   end, and the exit status is 1 if any error was reported. */
static int run_lines_from(Stack *stack, Stack *old_stack, FILE *f) {
  char *line = NULL;
  size_t cap = 0;
  ssize_t n;
  bool go_on = true;
  while (go_on && (n = getline(&line, &cap, f)) != -1) {
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    { const char *q = line; while (*q == ' ' || *q == '\t') q++; if (*q == '#') continue; }   /* a comment, or #! */
    go_on = handle_line(stack, old_stack, line);
    skip_stack_printing = false;
    completed_batch = false;
    if (go_on) tutor_after(stack);
  }
  free(line);
  return go_on;
}

static void usage_text(FILE *f) {
  fprintf(f,
    "usage: mm41 [-n] [-q] [-p] [-x] [-e EXPR]... [-f FILE]... [SCRIPT [ARG]...]\n"
    "       mm41 --modules | --commands | --commands-tex | --tutor-check | --version | -h\n"
    "\n"
    "  (no args)   interactive calculator (reads stdin when it is not a terminal)\n"
    "  -e EXPR     evaluate EXPR as one line; may be repeated\n"
    "  -f FILE     run the lines of FILE ('-' is stdin); may be repeated\n"
    "  SCRIPT      run the lines of SCRIPT (so #!/usr/bin/env mm41 works); the ARGs\n"
    "              after it are pushed first, numbers as numbers, the rest as strings;\n"
    "              quiet unless -p: a script prints what it wants with .\n"
    "  -           read the script from stdin\n"
    "  -q          quiet: do not print the final stack\n"
    "  -p          print the top of the stack plainly at the end (a number, rows of a matrix)\n"
    "  -x          stop at the first error\n"
    "  -n          do not load the saved words and config\n"
    "  --          end of options; what follows is a script name and its arguments\n"
    "\n"
    "In a script, # at the start of a line is a comment; `x .` prints a value plainly,\n"
    "`n exit` ends it with that status. Exit status: 0, or 1 if any error occurred,\n"
    "2 for a usage error or a file that cannot be opened.\n");
}

int main(int argc, char **argv) {
  enum { MAXJOBS = 256 };
  struct { char kind; const char *arg; } jobs[MAXJOBS];
  int njobs = 0;

  const char *script = NULL; int first_arg = argc;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "--")) { if (i + 1 < argc) { script = argv[i + 1]; first_arg = i + 2; } break; }
    if (a[0] != '-' || !strcmp(a, "-")) { script = a; first_arg = i + 1; break; }   /* a script and its arguments */
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage_text(stdout); return 0; }
    if (!strcmp(a, "--commands")) { load_modules(); print_ops_markdown(); return 0; }
    if (!strcmp(a, "--commands-tex")) { load_modules(); print_ops_latex(); return 0; }
    if (!strcmp(a, "--modules")) { load_modules(); list_modules(); return 0; }
    if (!strcmp(a, "--version")) { printf("MM-41 %s (%s)\n", MM41_VERSION, VERSION); return 0; }
    if (!strcmp(a, "--tutor-check")) {            /* every lesson against a fresh stack */
      Stack s1, s2; autoload = false; batch_mode = true;
      global_rng = gsl_rng_alloc(gsl_rng_mt19937);
      startup(&s1, &s2);
      if (access("data/modules/std.mm", R_OK) == 0) use_word_module("data/modules/std.mm");    /* what a session autoloads */
      if (access("data/modules/const.mm", R_OK) == 0) use_word_module("data/modules/const.mm");
      int bad = tutor_self_check(check_line); shutdown(&s1, &s2); gsl_rng_free(global_rng); return bad;
    }
    if (!strcmp(a, "-n")) { autoload = false; continue; }
    if (!strcmp(a, "-q")) { quiet_mode = true; continue; }
    if (!strcmp(a, "-p")) { print_top_mode = true; quiet_mode = true; continue; }
    if (!strcmp(a, "-x")) { stop_on_error = true; continue; }
    if ((!strcmp(a, "-e") || !strcmp(a, "-f")) && i + 1 < argc && njobs < MAXJOBS) {
      jobs[njobs].kind = a[1];
      jobs[njobs].arg = argv[++i];
      njobs++;
      continue;
    }
    usage_text(stderr);
    return 2;
  }

  global_rng = gsl_rng_alloc(gsl_rng_mt19937); // Init random number generation
  gsl_set_error_handler(&my_error_handler);

  if (script && njobs < MAXJOBS) { jobs[njobs].kind = 'f'; jobs[njobs].arg = script; njobs++; if (!print_top_mode) quiet_mode = true; }   /* a script prints what it wants with . */
  batch_mode = (njobs > 0) || !isatty(STDIN_FILENO);
  /* Line-buffer stdout in batch mode so output and error messages interleave
     in order and nothing is lost if a sanitizer aborts the process at exit. */
  if (batch_mode) setvbuf(stdout, NULL, _IOLBF, 0);
  if (!batch_mode) {
    repl();
    gsl_rng_free(global_rng);
    return 0;
  }

  Stack stack, old_stack;
  startup(&stack, &old_stack);
  bool go_on = true;
  for (int i = first_arg; i < argc; i++) {                 /* the script's arguments, pushed in order */
    char *end; double v = strtod(argv[i], &end);
    if (*argv[i] && *end == '\0') push_real(&stack, v); else push_string(&stack, argv[i]);
  }
  int open_failed = 0;
  if (njobs == 0) {
    go_on = run_lines_from(&stack, &old_stack, stdin);
  } else {
    for (int j = 0; j < njobs && go_on; j++) {
      if (jobs[j].kind == 'e') {
        char *copy = xstrdup(jobs[j].arg);
        go_on = handle_line(&stack, &old_stack, copy);
        free(copy);
      } else {
        FILE *f = strcmp(jobs[j].arg, "-") ? fopen(jobs[j].arg, "r") : stdin;
        if (!f) { mm_perror(jobs[j].arg); open_failed = 1; if (stop_on_error) break; continue; }
        go_on = run_lines_from(&stack, &old_stack, f);
        if (f != stdin) fclose(f);
      }
    }
  }
  if (print_top_mode) print_top_plain(&stack);
  else if (!quiet_mode) print_stack_display(&stack);
  shutdown(&stack, &old_stack);
  gsl_rng_free(global_rng);
  if (exit_status >= 0) return exit_status;
  if (open_failed) return 2;
  return error_count ? 1 : 0;
}
