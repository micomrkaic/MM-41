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

#include "errors.h"
#include "globals.h"
#include "decimal.h"

bool fixed_point = true;
bool verbose_mode = false;
double num_tol = DEFAULT_TOL;
long   num_evals = 0;
double num_last_err = 0.0;
bool completed_batch=false;
bool test_flag = false;
bool skip_stack_printing = false;
int print_precision = 6;
char holiday_calendar[64] = "";
char path_to_data_and_programs[MAX_PATH];

#include "globals.h"
#include <errno.h>            // for errno, EEXIST
#include <gsl/gsl_complex.h>  // for GSL_IMAG, GSL_REAL
#include <pwd.h>              // for getpwuid
#include <stdbool.h>          // for bool, false, true
#include <stdio.h>            // for fprintf, fclose, fopen, perror, stderr
#include <stdlib.h>           // for atoi, getenv
#include <string.h>           // for strcmp, strchr, strcspn, strncpy
#include <sys/stat.h>         // for mkdir
#include <unistd.h>           // for getuid

static int mkdir_if_missing(const char *p) {
  return (mkdir(p, 0700) == 0 || errno == EEXIST) ? 0 : -1;
}

const char *app_path(const char *file) {
  static char ring[4][MAX_PATH];
  static int slot = 0;
  char *out = ring[slot];
  slot = (slot + 1) % 4;

  /* base and dir are deliberately shorter than the output buffer so the
     compiler can prove the final snprintf does not truncate */
  char base[MAX_PATH / 2], dir[MAX_PATH / 2 + 8];
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) {
    snprintf(base, sizeof base, "%s", xdg);
  } else {
    const char *home = getenv("HOME");
    if (!home || !*home) {
      struct passwd *pw = getpwuid(getuid());
      home = (pw && pw->pw_dir) ? pw->pw_dir : ".";
    }
    snprintf(base, sizeof base, "%s/.config", home);
  }
  snprintf(dir, sizeof dir, "%.*s/mm41", (int)(sizeof base - 1), base);
  if (mkdir_if_missing(base) != 0 || mkdir_if_missing(dir) != 0)
    mm_warn("Warning: cannot create %s: %s\n", dir, strerror(errno));
  snprintf(out, MAX_PATH, "%s/%.255s", dir, file);
  return out;
}

int set_print_precision(Stack* stack) {
  if (stack->top < 0) {
    mm_error("Error: Stack underflow. Expected number for precision\n");
    return 1;
  }

  stack_coerce_real(stack, 0);
  const stack_element *elem = &stack->items[stack->top];
  if (elem->type != TYPE_REAL) {
    mm_error("setprec: expected a real number");
    return 1;
  }
  int precision = (int)elem->real;
  if (precision < 0 || precision > 20) {
    mm_error("Error: Precision value %d is out of valid range (0–20)\n", precision);
    return 1;
  }
  stack->top--;                      /* a real: nothing to free */
  print_precision = precision;
  return 0;
}

void swap_fixed_scientific(void) {
  fixed_point = !fixed_point;
}

void save_config(const char* filename) {
  FILE* f = fopen(filename, "w");
  if (!f) {
    mm_perror("Failed to open config file for writing");
    return;
  }

  fprintf(f, "print_precision = %d\n", print_precision);
  fprintf(f, "fixed_point = %d\n", fixed_point);
  fprintf(f, "verbose_mode = %d\n", verbose_mode);
  fprintf(f, "tol = %g\n", num_tol);
  fprintf(f, "number_mode = %s\n", decimal_mode ? "decimal" : "binary");
  if (holiday_calendar[0]) fprintf(f, "holidays = %s\n", holiday_calendar);

  fclose(f);
}

int load_config(const char* filename) {
  //  printf("Config file: %s\n", filename);
  FILE* f = fopen(filename, "r");
  if (!f) {
    if (errno != ENOENT) mm_perror(filename);      /* first run: no config yet */
    return 1;
  }

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    // Remove trailing newline
    line[strcspn(line, "\r\n")] = '\0';

    char* equal_sign = strchr(line, '=');
    if (!equal_sign) continue;

    *equal_sign = '\0';
    char* key = line;
    char* value = equal_sign + 1;

    /* Trim the key: the file is written as "key = value", and the space
       before '=' used to end up in the key, so nothing ever matched and no
       setting survived a restart. */
    while (*key == ' ' || *key == '\t') key++;
    size_t klen = strlen(key);
    while (klen > 0 && (key[klen - 1] == ' ' || key[klen - 1] == '\t')) key[--klen] = '\0';
    value[strcspn(value, "\r\n")] = '\0';
    size_t vlen = strlen(value);
    while (vlen > 0 && (value[vlen - 1] == ' ' || value[vlen - 1] == '\t')) value[--vlen] = '\0';

    // Trim leading spaces from value
    while (*value == ' ') value++;

    if (strcmp(key, "print_precision") == 0) {
      print_precision = atoi(value);
    } else if (strcmp(key, "fixed_point") == 0) {
      fixed_point = atoi(value);
    } else if (strcmp(key, "number_mode") == 0) {
      decimal_mode = strcmp(value, "decimal") == 0;
    } else if (strcmp(key, "holidays") == 0) {
      snprintf(holiday_calendar, sizeof holiday_calendar, "%s", value);
    } else if (strcmp(key, "verbose_mode") == 0) {
      verbose_mode = atoi(value);
    } else if (strcmp(key, "tol") == 0) {
      double t = atof(value);
      if (t > 0.0 && t < 1.0) num_tol = t;
    } else if (strcmp(key, "path_to_data_and_programs") == 0) {
      strncpy(path_to_data_and_programs, value, MAX_PATH - 1);
      path_to_data_and_programs[MAX_PATH - 1] = '\0';
    }
  }

  fclose(f);
  return 0;
}
