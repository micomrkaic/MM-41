/*
 * nomem_shim.c -- make chosen allocations fail, to test what MM-41 does
 * when memory runs out. Loaded with LD_PRELOAD by tests/nomem.py.
 *
 *   MM41_NOMEM_AT=N     the N-th counted allocation fails (returns NULL)
 *   MM41_NOMEM_RUN=K    and the K-1 counted allocations after it (default 1)
 *   MM41_NOMEM_SCOPE    "cmd": count only allocations made while a command
 *                       is running, that is while the calculator has a
 *                       recovery point set (mm_nomem_escape, errors.h);
 *                       "all": count every allocation from the first
 *                       command on, the interpreter's own included
 *
 * When an allocation is failed, a line "NOMEM: failed #N (size)" goes to
 * stderr; at exit "NOMEM: counted M" says how many there were, so the
 * driver knows when it has tried them all.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static void (*real_free)(void *);
static void **escape;              /* &mm_nomem_escape in the calculator */
static long at = -1, run = 1, counted;
static int scope_all, started, ready, starting;

static char boot[4096];            /* dlsym may allocate before the real functions are known */
static size_t boot_used;

static void say(const char *what, long a, size_t b) {
  char line[96];
  int n = snprintf(line, sizeof line, "NOMEM: %s %ld (%zu)\n", what, a, b);
  if (n > 0 && write(2, line, (size_t)n) < 0) { /* nothing to do about it */ }
}

__attribute__((destructor)) static void at_exit(void) { if (escape) say("counted", counted, 0); }

static void init(void) {
  starting = 1;
  real_malloc = (void *(*)(size_t))dlsym(RTLD_NEXT, "malloc");
  real_calloc = (void *(*)(size_t, size_t))dlsym(RTLD_NEXT, "calloc");
  real_realloc = (void *(*)(void *, size_t))dlsym(RTLD_NEXT, "realloc");
  real_free = (void (*)(void *))dlsym(RTLD_NEXT, "free");
  escape = (void **)dlsym(RTLD_DEFAULT, "mm_nomem_escape");
  starting = 0;
  ready = 1;
}

/* 1 if this allocation is to fail */
static int fails(size_t size) {
  if (!escape) return 0;
  int in_command = *escape != NULL;
  static int configured;
  if (!configured && in_command) {   /* not earlier: the first allocations come before the environment exists */
    const char *e = getenv("MM41_NOMEM_AT");
    if (e) at = atol(e);
    e = getenv("MM41_NOMEM_RUN");
    if (e) run = atol(e);
    e = getenv("MM41_NOMEM_SCOPE");
    scope_all = e && strcmp(e, "all") == 0;
    configured = 1;
  }
  if (in_command) started = 1;
  if (scope_all ? !started : !in_command) return 0;
  counted++;
  if (at > 0 && counted >= at && counted < at + run) { say("failed", counted, size); return 1; }
  return 0;
}

void *malloc(size_t size) {
  if (!ready) {
    if (!starting) init();
    if (!ready) {                                     /* called from inside dlsym */
      size_t need = (size + 15) & ~(size_t)15;
      if (boot_used + need > sizeof boot) return NULL;
      void *p = boot + boot_used;
      boot_used += need;
      return p;
    }
  }
  if (fails(size)) return NULL;
  return real_malloc(size);
}

void *calloc(size_t count, size_t size) {
  if (!ready) {
    if (!starting) init();
    if (!ready) {
      void *p = malloc(count * size);
      if (p) memset(p, 0, count * size);
      return p;
    }
  }
  if (fails(count * size)) return NULL;
  return real_calloc(count, size);
}

void *realloc(void *p, size_t size) {
  if (!ready && !starting) init();
  if (!ready) {                                       /* inside dlsym: nothing to grow from but the boot area */
    void *q = malloc(size);
    if (q && p) memcpy(q, p, size);                   /* never beyond the boot area, which is all there is to copy from */
    return q;
  }
  if (size && fails(size)) return NULL;
  return real_realloc(p, size);
}

void free(void *p) {
  if ((char *)p >= boot && (char *)p < boot + sizeof boot) return;
  if (real_free) real_free(p);
}
