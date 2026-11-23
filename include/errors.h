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

#ifndef ERRORS_H
#define ERRORS_H

/* The single error channel.
 *
 * mm_error() prints the message on stderr (prefixed with "Error: " unless
 * the message already starts with it), and bumps error_count. The
 * interpreter compares error_count before and after each token: if it
 * grew, the stack is rolled back to its state before that token and the
 * rest of the line is skipped. In batch mode a nonzero count at exit
 * gives exit status 1.
 *
 * mm_error() returns -1 so "return mm_error(...)" works as an error return.
 * mm_warn() prints "Warning: ..." and does not count. */
int  mm_error_say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* mm_error is the call followed by a constant -1, so that the compiler and
 * the static analyzer can see what every caller relies on: the value is
 * never zero. Without it `gcc -fanalyzer` cannot rule out a reader that
 * reports an error and returns "success", and reports each use of its
 * outputs as uninitialized. */
static inline int mm_error_value(void) { return -1; }
#define mm_error(...) (mm_error_say(__VA_ARGS__), mm_error_value())
void mm_warn(const char *fmt, ...)  __attribute__((format(printf, 1, 2)));
#include <stdbool.h>
extern long error_count;
extern bool errors_quiet;      /* suppress the message (help runs examples silently) */

#include <errno.h>
#include <string.h>
#define mm_perror(what) mm_error("%s: %s", (what), strerror(errno))

/* Small allocations that the code has no way to do without: a line of text,
 * a list of names, a work vector the size of an argument already on the
 * stack. These return memory or do not return: when the machine has none
 * left they say so and stop the calculator, rather than hand back a null
 * pointer for the next line to write through. Allocations whose size comes
 * straight from the user (a matrix, a table of draws) are checked where they
 * are made and fail with an ordinary error instead. */
#include <stddef.h>
#include <setjmp.h>

/* Out of memory in the middle of a command.
 *
 * GSL reports a failed allocation to the error handler and returns a null
 * pointer, and about two hundred places allocate through it. Rather than
 * rely on every one of them checking, the token evaluator sets a recovery
 * point around each command (mm_nomem_escape) and the handler jumps to it:
 * the command is abandoned, an error is reported, and the interpreter
 * restores the stack from the snapshot it took before the token, as it
 * does after any error. What the command had allocated up to that point is
 * not returned -- a bounded leak, on a path that used to be a crash.
 *
 * With no recovery point set (start-up, reading files, the interpreter's
 * own bookkeeping between tokens) mm_out_of_memory says so and exits.
 *
 * A command must not leave a freed pointer in the stack or in a register
 * while it allocates: the rollback would free it again. tests/nomem.py
 * fails allocations one by one to check that none does. */
extern jmp_buf *mm_nomem_escape;
_Noreturn void mm_out_of_memory(void);
void my_error_handler(const char *reason, const char *file, int line, int gsl_errno);   /* the GSL error handler (main.c) */

/* For a stretch of GSL calls whose failures are expected and are read from
 * their return values (a minimiser wandering out of the domain, an integral
 * that does not converge): like gsl_set_error_handler_off(), which it
 * replaces, but running out of memory is still not allowed to pass as a
 * null pointer. Returns the previous handler, to be put back with
 * gsl_set_error_handler() as before. */
typedef void mm_gsl_handler(const char *reason, const char *file, int line, int gsl_errno);
mm_gsl_handler *mm_gsl_errors_off(void);

void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *p, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

#endif
