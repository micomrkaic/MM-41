/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The module registry and the flat dispatch table it builds. */

#include "errors.h"
#include "module.h"
#include "words.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODULES 64

static const Module *registry[MAX_MODULES];
static int           first_op[MAX_MODULES];   /* index in ops[] of each module's first command */
static int           n_modules = 0;

static Op *flat_table = NULL;   /* what ops points at */
const Op *ops   = NULL;
int       n_ops = 0;

int modules_register(const Module *m) {
  if (!m || !m->name) return mm_error("module: missing name");
  if (n_modules >= MAX_MODULES) return mm_error("module %s: too many modules", m->name);
  for (int i = 0; i < n_modules; i++)
    if (strcmp(registry[i]->name, m->name) == 0)
      return mm_error("module %s: already registered", m->name);
  registry[n_modules++] = m;
  return 0;
}

static int rebuild_table(void) {
  int total = 0;
  for (int i = 0; i < n_modules; i++) total += registry[i]->n_ops;
  Op *flat = calloc((size_t)(total ? total : 1), sizeof *flat);
  if (!flat) return mm_error("module registry: out of memory");
  int k = 0;
  for (int i = 0; i < n_modules; i++) {
    first_op[i] = k;
    for (int j = 0; j < registry[i]->n_ops; j++) {
      const Op *o = &registry[i]->ops[j];
      for (int p = 0; p < k; p++)
        if (strcmp(flat[p].name, o->name) == 0) {
          free(flat);
          return mm_error("module %s: command '%s' is already defined by module %s",
                          registry[i]->name, o->name, module_of(p) ? module_of(p)->name : "?");
        }
      flat[k++] = *o;
    }
  }
  free(flat_table);
  flat_table = flat;
  ops = flat;
  n_ops = k;
  return 0;
}

int modules_init_all(void) {
  if (rebuild_table() != 0) return -1;
  for (int i = 0; i < n_modules; i++)
    if (registry[i]->init && registry[i]->init() != 0)
      return mm_error("module %s: initialisation failed", registry[i]->name);
  return 0;
}

void modules_shutdown_all(void) {
  for (int i = n_modules - 1; i >= 0; i--)
    if (registry[i]->shutdown) registry[i]->shutdown();
  free(flat_table);
  flat_table = NULL;
  ops = NULL;
  n_ops = 0;
}

int modules_count(void) { return n_modules; }
const Module *module_at(int i) { return (i >= 0 && i < n_modules) ? registry[i] : NULL; }

const Module *module_find(const char *name) {
  for (int i = 0; i < n_modules; i++)
    if (strcmp(registry[i]->name, name) == 0) return registry[i];
  return NULL;
}

const Module *module_of(int idx) {
  for (int i = n_modules - 1; i >= 0; i--)
    if (idx >= first_op[i] && idx < first_op[i] + registry[i]->n_ops) return registry[i];
  return NULL;
}

int module_first_op(const Module *m) {
  for (int i = 0; i < n_modules; i++)
    if (registry[i] == m) return first_op[i];
  return -1;
}

void list_modules(void) {
  printf("%-12s %-8s %5s  %s\n", "module", "version", "words", "summary");
  for (int i = 0; i < n_modules; i++)
    printf("%-12s %-8s %5d  %s\n", registry[i]->name, registry[i]->version,
           registry[i]->n_ops, registry[i]->summary ? registry[i]->summary : "");
  list_word_modules();
}
