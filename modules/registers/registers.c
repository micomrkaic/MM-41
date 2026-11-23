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
#include <math.h>
#include "errors.h"
#include <gsl/gsl_complex.h>                // for GSL_IMAG, GSL_REAL, gsl_c...
#include <gsl/gsl_complex_math.h>           // for gsl_complex_rect
#include <gsl/gsl_matrix_complex_double.h>  // for gsl_matrix_complex_alloc
#include <gsl/gsl_matrix_double.h>          // for gsl_matrix_alloc, gsl_mat...
#include <stdbool.h>                        // for false, true
#include <stdio.h>                          // for fprintf, printf, sscanf
#include <stdlib.h>                         // for size_t, NULL, free
#include <string.h>                         // for strchr, strcmp, strdup
#include "stack.h"                          // for (anonymous struct)::(anon...
#include "registers.h"
#include "ops.h"                           // for MM41_MAX_CELLS                      // for registers, MAX_REG, Register

Register registers[MAX_REG] = {0};

stack_element copy_element(const stack_element* src) {
  stack_element copy;
  copy.type = src->type;

  switch (src->type) {
  case TYPE_REAL:
    copy.real = src->real;
    break;

  case TYPE_COMPLEX:
    copy.complex_val = src->complex_val;
    break;

  case TYPE_DECIMAL:
    copy.dec = src->dec;
    break;

  case TYPE_STRING:
    copy.string = xstrdup(src->string);
    break;

  case TYPE_MATRIX_REAL:
    if (src->matrix_real) {
      size_t r = src->matrix_real->size1;
      size_t c = src->matrix_real->size2;
      copy.matrix_real = gsl_matrix_alloc(r, c);
      gsl_matrix_memcpy(copy.matrix_real, src->matrix_real);
    } else {
      copy.matrix_real = NULL;
    }
    break;

  case TYPE_MATRIX_COMPLEX:
    if (src->matrix_complex) {
      size_t r = src->matrix_complex->size1;
      size_t c = src->matrix_complex->size2;
      copy.matrix_complex = gsl_matrix_complex_alloc(r, c);
      gsl_matrix_complex_memcpy(copy.matrix_complex, src->matrix_complex);
    } else {
      copy.matrix_complex = NULL;
    }
    break;
  }

  return copy;
}


void free_element(stack_element* el) {
  switch (el->type) {
  case TYPE_STRING:
    free(el->string);
    break;
  case TYPE_MATRIX_REAL:
    gsl_matrix_free(el->matrix_real);
    break;
  case TYPE_MATRIX_COMPLEX:
    gsl_matrix_complex_free(el->matrix_complex);
    break;
  default:
    break;
  }
  el->type = TYPE_REAL;
  el->real = 0.0;
}


/* value n sto : store the value below the index, as on the HP (1.x took
   "n value sto"). Both items are consumed; the stored copy is independent. */
void store_to_register(Stack* stack) {
  if (stack->top < 1) {
    mm_error("sto: need a value and a register index on the stack (value n sto)");
    return;
  }
  stack_coerce_real(stack, 0);
  stack_element* reg_elem = &stack->items[stack->top];
  stack_element* value_elem = &stack->items[stack->top - 1];

  if (reg_elem->type != TYPE_REAL || reg_elem->real != floor(reg_elem->real)) {
    mm_error("sto: register index must be an integer (value n sto)");
    return;
  }
  if (!(reg_elem->real >= 0 && reg_elem->real < MAX_REG)) {
    mm_error("sto: invalid register index %g (0..%d)", reg_elem->real, MAX_REG - 1);
    return;
  }
  int reg_index = (int)reg_elem->real;

  if (registers[reg_index].occupied) free_element(&registers[reg_index].value);
  registers[reg_index].value = *value_elem;      /* take ownership, no copy */
  registers[reg_index].occupied = true;
  stack->top -= 2;                               /* index is a real: nothing to free */
}

void recall_from_register(Stack* stack) {
  if (stack->top < 0) {
    mm_error("Stack underflow: need register index.\n");
    return;
  }

  stack_coerce_real(stack, 0);
  const stack_element *reg_elem = &stack->items[stack->top];

  if (reg_elem->type != TYPE_REAL) {
    mm_error("Type error: register index must be a real number.\n");
    return;
  }

  if (!(reg_elem->real > -1 && reg_elem->real < MAX_REG)) {     /* a fraction is cut off, as before; nan fails */
    mm_error("Invalid register index: %g\n", reg_elem->real);
    return;
  }
  int reg_index = (int)reg_elem->real;

  if (!registers[reg_index].occupied) {
    mm_error("Register %d is empty.\n", reg_index);
    return;
  }

  stack->top--;                      /* the index is a real: nothing to free */
  stack_element copy = copy_element(&registers[reg_index].value);
  stack->items[++stack->top] = copy;
}

void show_registers_status(void) {
  printf("Register status (%d total):\n", MAX_REG);
  for (int i = 0; i < MAX_REG; ++i) {
    printf("[%c]", registers[i].occupied ? 'x' : ' ');
    if ((i + 1) % 8 == 0) {
      printf(" -> R[%2d–%2d]\n", i - 7, i);
    }
  }
  if (MAX_REG % 8 != 0) printf("\n");  // final newline if not aligned
}

void init_registers(void) {
  for (int i = 0; i < MAX_REG; ++i) {
    registers[i].occupied = false;
    registers[i].value.type = TYPE_REAL;
    registers[i].value.real = 0.0;
  }
}

void free_all_registers(void) {
  for (int i = 0; i < MAX_REG; ++i) {
    if (registers[i].occupied) {
      free_element(&registers[i].value);  // deep free
      registers[i].occupied = false;
    }
  }
}

void save_registers_to_file(const char* filename) {
  FILE* f = fopen(filename, "w");
  if (!f) {
    mm_perror("fopen");
    return;
  }

  for (int i = 0; i < MAX_REG; ++i) {
    if (!registers[i].occupied) continue;

    stack_element* el = &registers[i].value;
    fprintf(f, "REG %d ", i);

    switch (el->type) {
    case TYPE_REAL:
      fprintf(f, "REAL %.17g\n", el->real);
      break;
    case TYPE_COMPLEX:
      fprintf(f, "COMPLEX (%.17g,%.17g)\n", GSL_REAL(el->complex_val), GSL_IMAG(el->complex_val));
      break;
    case TYPE_DECIMAL: {
      char buf[DECNUMDIGITS + 16];
      dec_to_string(&el->dec, buf, sizeof buf);
      fprintf(f, "DECIMAL %s\n", buf);
      break;
    }
    case TYPE_STRING:
      fprintf(f, "STRING \"%s\"\n", el->string);
      break;
    case TYPE_MATRIX_REAL: {
      gsl_matrix* m = el->matrix_real;
      fprintf(f, "MATRIX_REAL %zu %zu", m->size1, m->size2);
      for (size_t i_cnt = 0; i_cnt < m->size1 * m->size2; ++i_cnt) {
	fprintf(f, " %.17g", m->data[i_cnt]);
      }
      fprintf(f, "\n");
      break;
    }
    case TYPE_MATRIX_COMPLEX: {
      gsl_matrix_complex* m = el->matrix_complex;
      fprintf(f, "MATRIX_COMPLEX %zu %zu", m->size1, m->size2);
      for (size_t i_cnt = 0; i_cnt < m->size1; ++i_cnt) {
        for (size_t j = 0; j < m->size2; ++j) {
	  gsl_complex z = gsl_matrix_complex_get(m, i_cnt, j);
	  fprintf(f, " (%.17g,%.17g)", GSL_REAL(z), GSL_IMAG(z));
        }
      }
      fprintf(f, "\n");     /* without it the next register shared this line and was lost on loading */
      break;
    }
    }
  }
  fclose(f);
  printf("Registers saved to %s\n", filename);
}

/* One number at *p, which moves past it; false if there is none. */
static bool next_number(char **p, double *out) {
  char *end;
  *out = strtod(*p, &end);
  if (end == *p) return false;
  *p = end;
  return true;
}

/* The registers file is read line by line, and a line is taken only if
 * every part of it is there: the index in range, the type known, the sizes
 * of a matrix sane and as many values as they call for. Anything else is
 * skipped and counted. The file is the user's and may have been edited, cut
 * short by a full disk, or written by another version; the loader this
 * replaces trusted it, and a line it did not expect could crash the
 * calculator or make it ask for all the memory there is. Lines are of any
 * length (a 100 x 100 matrix is one line of some 200,000 characters, which
 * the old 4096-byte buffer cut into pieces). */
void load_registers_from_file(const char* filename) {
  FILE* f = fopen(filename, "r");
  if (!f) {
    mm_perror("fopen");
    return;
  }

  // Clear all first
  free_all_registers();

  char *line = NULL;
  size_t cap = 0;
  int skipped = 0;
  while (getline(&line, &cap, f) != -1) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') continue;

    int index = -1, used = 0;
    char type[32];
    if (sscanf(p, "REG %d %31s%n", &index, type, &used) != 2 || index < 0 || index >= MAX_REG) { skipped++; continue; }
    p += used;

    stack_element el;
    memset(&el, 0, sizeof el);
    bool ok = false;
    if (strcmp(type, "REAL") == 0) {
      el.type = TYPE_REAL;
      ok = next_number(&p, &el.real);
    } else if (strcmp(type, "COMPLEX") == 0) {
      double re, im;
      ok = sscanf(p, " (%lf,%lf)", &re, &im) == 2;
      el.type = TYPE_COMPLEX;
      if (ok) el.complex_val = gsl_complex_rect(re, im);
    } else if (strcmp(type, "DECIMAL") == 0) {
      char buf[128];
      ok = sscanf(p, "%127s", buf) == 1 && dec_from_string(&el.dec, buf) == 0;
      el.type = TYPE_DECIMAL;
    } else if (strcmp(type, "STRING") == 0) {
      char* start = strchr(p, '"');
      char* end = strrchr(p, '"');
      if (start && end && end > start) {
        *end = '\0';
        el.type = TYPE_STRING;
        el.string = strdup(start + 1);
        ok = el.string != NULL;
      }
    } else if (strcmp(type, "MATRIX_REAL") == 0 || strcmp(type, "MATRIX_COMPLEX") == 0) {
      bool cplx = type[7] == 'C';
      double r, c;
      if (next_number(&p, &r) && next_number(&p, &c) && r >= 1 && c >= 1 && r == floor(r) && c == floor(c)
          && r * c <= (double)MM41_MAX_CELLS) {
        size_t rows = (size_t)r, cols = (size_t)c, n = rows * cols, i = 0;
        if (!cplx) {
          gsl_matrix *m = gsl_matrix_alloc(rows, cols);
          if (m) {
            for (; i < n; i++) { double v; if (!next_number(&p, &v)) break; gsl_matrix_set(m, i / cols, i % cols, v); }
            if (i == n) { el.type = TYPE_MATRIX_REAL; el.matrix_real = m; ok = true; } else gsl_matrix_free(m);
          }
        } else {
          gsl_matrix_complex *m = gsl_matrix_complex_alloc(rows, cols);
          if (m) {
            for (; i < n; i++) {
              double re, im; int len = 0;
              if (sscanf(p, " (%lf,%lf)%n", &re, &im, &len) != 2 || len == 0) break;
              p += len;
              gsl_matrix_complex_set(m, i / cols, i % cols, gsl_complex_rect(re, im));
            }
            if (i == n) { el.type = TYPE_MATRIX_COMPLEX; el.matrix_complex = m; ok = true; } else gsl_matrix_complex_free(m);
          }
        }
      }
    }
    if (!ok) { skipped++; continue; }

    if (registers[index].occupied) free_element(&registers[index].value);   /* the same register twice: the last one stays */
    registers[index].value = el;
    registers[index].occupied = true;
  }
  free(line);
  fclose(f);
  if (skipped) mm_error("%d line%s of %s could not be read and %s skipped", skipped, skipped == 1 ? "" : "s", filename, skipped == 1 ? "was" : "were");
  printf("Registers loaded from %s\n", filename);
}

#include <stdbool.h>

#define MAX_REG 64

extern Register registers[MAX_REG];  // Your global register array

void find_first_free_register(Stack *stack) {
  for (int i = 0; i < MAX_REG; ++i) {
    if (!registers[i].occupied) {
      push_real(stack,(double)i);
      return;
    }
  }
  printf("All registers are occupied\n");    
}
