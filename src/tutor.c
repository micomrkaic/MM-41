/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The tutorial. doc/tutor.txt is a book of chapters, compiled into the
   binary (or read from $MM41_TUTOR while lessons are being written):

     = The stack                  a chapter
     prose ...                    printed before the step
     > 3 4 +                      the line to type (optional)
     ? 7                          what the stack must show afterwards; ends the step

   `tutor` shows a step and waits; after every line you type it looks at the
   stack, and when the expectation holds it moves on. You may type anything
   -- the lesson only checks the result. `examples` prints a chapter as a
   cookbook: the same prose, each line with its result.

   Expectations:  a number (within 1e-6 relative)   "a string"   complex
   matrix [RxC]   empty   depth N   word NAME   near X D (|top-X| < D)   any */

#define _POSIX_C_SOURCE 200809L
#include "tutor.h"
#include "ansi_colors.h"
#include "errors.h"
#include "decimal.h"
#include "eval_fun.h"
#include "globals.h"
#include "print_fun.h"
#include "words.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern const char mm41_tutor_text[];      /* generated from doc/tutor.txt */

typedef struct { char *prose; char *cmd; char *want; } Step;
typedef struct { char *title; int first, n; } Chapter;

static Step *steps; static int n_steps, cap_steps;
static Chapter *chapters; static int n_chapters, cap_chapters;
static bool loaded;

char tutor_progress[32] = "";

/* ---- the book -------------------------------------------------------- */

static void add_step(char **prose, char **cmd, char *want) {
  if (n_chapters == 0) { free(*prose); free(*cmd); *prose = *cmd = NULL; return; }
  if (n_steps == cap_steps) { cap_steps = cap_steps ? 2 * cap_steps : 64; steps = xrealloc(steps, (size_t)cap_steps * sizeof *steps); }
  char *pr = *prose ? *prose : xstrdup("");
  { size_t k = 0; while (pr[k] == '\n') k++; if (k) memmove(pr, pr + k, strlen(pr + k) + 1); }
  for (size_t n = strlen(pr); n > 1 && pr[n - 1] == '\n' && pr[n - 2] == '\n'; n--) pr[n - 1] = '\0';
  steps[n_steps].prose = pr;
  steps[n_steps].cmd = *cmd;
  steps[n_steps].want = want;
  n_steps++;
  chapters[n_chapters - 1].n++;
  *prose = NULL; *cmd = NULL;
}

static void append_line(char **buf, const char *line) {
  size_t have = *buf ? strlen(*buf) : 0, add = strlen(line);
  *buf = xrealloc(*buf, have + add + 2);
  memcpy(*buf + have, line, add);
  (*buf)[have + add] = '\n';
  (*buf)[have + add + 1] = '\0';
}

static void parse(const char *text) {
  char *prose = NULL, *cmd = NULL;
  const char *p = text;
  while (*p) {
    const char *e = strchr(p, '\n');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    char *line = xmalloc(len + 1);
    memcpy(line, p, len); line[len] = '\0';
    p = e ? e + 1 : p + len;
    if (line[0] == '=' && line[1] == ' ') {
      free(prose); free(cmd); prose = cmd = NULL;
      if (n_chapters == cap_chapters) { cap_chapters = cap_chapters ? 2 * cap_chapters : 16; chapters = xrealloc(chapters, (size_t)cap_chapters * sizeof *chapters); }
      chapters[n_chapters].title = xstrdup(line + 2);
      chapters[n_chapters].first = n_steps;
      chapters[n_chapters].n = 0;
      n_chapters++;
    } else if (line[0] == '>' && (line[1] == ' ' || line[1] == '\0')) {
      free(cmd); cmd = xstrdup(line[1] ? line + 2 : "");
    } else if (line[0] == '?' && (line[1] == ' ' || line[1] == '\0')) {
      add_step(&prose, &cmd, xstrdup(line[1] ? line + 2 : "any"));
    } else if (line[0] == ';' && line[1] == ';') {
      /* a comment for the lesson writer */
    } else {
      append_line(&prose, line);
    }
    free(line);
  }
  free(prose); free(cmd);
}

static void load_book(void) {
  if (loaded) return;
  loaded = true;
  const char *env = getenv("MM41_TUTOR");
  if (env && *env) {
    FILE *f = fopen(env, "r");
    if (f) {
      char *buf = NULL; size_t cap = 0, n = 0; int c;
      while ((c = fgetc(f)) != EOF) { if (n + 2 > cap) { cap = cap ? 2 * cap : 4096; buf = xrealloc(buf, cap); } buf[n++] = (char)c; }
      fclose(f);
      if (buf) { buf[n] = '\0'; parse(buf); free(buf); return; }
    }
    mm_error("tutor: cannot read $MM41_TUTOR (%s); using the built-in book", env);
  }
  parse(mm41_tutor_text);
}

/* ---- the state --------------------------------------------------------- */

static bool active, handled_cmd;      /* handled_cmd: this line was skip/back/..., so tutor_after leaves it */
static int cur_ch = 0, cur_st = 0;     /* 0-based; cur_st == n means "end of chapter" */
static int fails;
static Stack snap; static bool have_snap;   /* the stack as the current step found it, for retry */

static void take_snapshot(const Stack *s) {
  if (!have_snap) { init_stack(&snap); have_snap = true; }
  if (s) copy_stack(&snap, s);
}

bool tutor_active(void) { return active; }

static void save_progress(void) {
  snprintf(tutor_progress, sizeof tutor_progress, "%d.%d", cur_ch + 1, cur_st + 1);
}

static void rule(const char *title, int ch) {
  char head[160];
  snprintf(head, sizeof head, "%d  %s", ch + 1, title);
  printf("\n" BOLD "%s" RESET "  ", head);
  int w = 78 - (int)strlen(head) - 2;
  for (int i = 0; i < w; i++) putchar('-');
  printf("  (%d/%d)\n", ch + 1, n_chapters);
}

static void show_step(void) {
  const Chapter *c = &chapters[cur_ch];
  if (cur_st >= c->n) {
    if (cur_ch + 1 < n_chapters)
      printf("\n  That is chapter %d. Enter continues with %d, %s; q leaves (tutor picks up here later).\n",
             cur_ch + 1, cur_ch + 2, chapters[cur_ch + 1].title);
    else
      printf("\n  That was the last chapter. You have been through the whole book; tutor 1 starts again.\n");
    return;
  }
  const Step *s = &steps[c->first + cur_st];
  if (cur_st == 0) rule(c->title, cur_ch);
  if (s->prose[0]) printf("\n%s", s->prose);
  if (s->cmd) printf("\n  " BOLD "type:" RESET "  %s\n", s->cmd);
  else if (strcmp(s->want, "any") == 0) printf("\n  (Enter continues)\n");
  fails = 0;
}

static void start_chapter(int ch, const Stack *stack) {
  cur_ch = ch; cur_st = 0; active = true;
  save_progress();
  show_step();
  take_snapshot(stack);
}

/* ---- expectations -------------------------------------------------------- */

static bool top_as_real(const Stack *s, double *x) {
  if (s->top < 0) return false;
  const stack_element *e = &s->items[s->top];
  if (e->type == TYPE_REAL) { *x = e->real; return true; }
  if (e->type == TYPE_DECIMAL) { *x = dec_to_double(&e->dec); return true; }
  return false;
}

static bool close_to(double a, double b, double rel) {
  double scale = fabs(b) > 1 ? fabs(b) : 1;
  return fabs(a - b) <= rel * scale;
}

/* true when the expectation holds; `why` gets a short explanation otherwise */
static bool satisfied(const char *want, Stack *s, char *why, size_t cap) {
  char w[256]; snprintf(w, sizeof w, "%s", want);
  for (char *e = w + strlen(w); e > w && isspace((unsigned char)e[-1]); e--) e[-1] = '\0';
  const stack_element *top = s->top >= 0 ? &s->items[s->top] : NULL;
  double x;
  why[0] = '\0';
  if (strcmp(w, "any") == 0) return true;
  if (strcmp(w, "empty") == 0) {
    if (s->top < 0) return true;
    snprintf(why, cap, "the stack should be empty; it has %d item%s", s->top + 1, s->top ? "s" : "");
    return false;
  }
  if (strncmp(w, "depth ", 6) == 0) {
    int d = atoi(w + 6);
    if (s->top + 1 == d) return true;
    snprintf(why, cap, "the stack should have %d item%s; it has %d", d, d == 1 ? "" : "s", s->top + 1);
    return false;
  }
  if (strncmp(w, "word ", 5) == 0) {
    if (find_word(w + 5)) return true;
    snprintf(why, cap, "the word %s is not defined yet", w + 5);
    return false;
  }
  if (strcmp(w, "complex") == 0) {
    if (top && top->type == TYPE_COMPLEX) return true;
    snprintf(why, cap, "the top should be a complex number");
    return false;
  }
  if (strcmp(w, "string") == 0) {
    if (top && top->type == TYPE_STRING) return true;
    snprintf(why, cap, "the top should be a string");
    return false;
  }
  if (strncmp(w, "matrix", 6) == 0) {
    if (!top || (top->type != TYPE_MATRIX_REAL && top->type != TYPE_MATRIX_COMPLEX)) { snprintf(why, cap, "the top should be a matrix"); return false; }
    int r = 0, c = 0;
    if (sscanf(w + 6, " %dx%d", &r, &c) == 2) {
      size_t rr = top->type == TYPE_MATRIX_REAL ? top->matrix_real->size1 : top->matrix_complex->size1;
      size_t cc = top->type == TYPE_MATRIX_REAL ? top->matrix_real->size2 : top->matrix_complex->size2;
      if ((int)rr != r || (int)cc != c) { snprintf(why, cap, "the matrix should be %d x %d; it is %zu x %zu", r, c, rr, cc); return false; }
    }
    return true;
  }
  if (w[0] == '"') {
    size_t n = strlen(w);
    if (n >= 2 && w[n - 1] == '"') w[n - 1] = '\0';
    if (top && top->type == TYPE_STRING && strcmp(top->string, w + 1) == 0) return true;
    snprintf(why, cap, "the top should be the string \"%s\"", w + 1);
    return false;
  }
  if (strncmp(w, "near ", 5) == 0) {
    double target = 0, d = 0;
    if (sscanf(w + 5, "%lf %lf", &target, &d) != 2) return true;
    if (top_as_real(s, &x) && fabs(x - target) < d) return true;
    snprintf(why, cap, "the top should be a number within %g of %g", d, target);
    return false;
  }
  {
    char *end; double target = strtod(w, &end);
    if (end != w) {
      if (!top) { snprintf(why, cap, "the stack is empty; the lesson expects %s on top", w); return false; }
      if (!top_as_real(s, &x)) { snprintf(why, cap, "the top should be the number %s", w); return false; }
      if (close_to(x, target, 1e-6)) return true;
      snprintf(why, cap, "the top is %.10g; the lesson expects %s", x, w);
      return false;
    }
  }
  return true;                                  /* an expectation this version does not know: let it pass */
}

/* ---- the interactive part ------------------------------------------------- */

void tutor_after(Stack *stack) {
  if (handled_cmd) { handled_cmd = false; return; }
  if (!active) return;
  const Chapter *c = &chapters[cur_ch];
  if (cur_st >= c->n) {                        /* between chapters: anything moves on */
    if (cur_ch + 1 < n_chapters) start_chapter(cur_ch + 1, stack);
    else { active = false; snprintf(tutor_progress, sizeof tutor_progress, "done"); }
    return;
  }
  const Step *s = &steps[c->first + cur_st];
  char why[256];
  if (satisfied(s->want, stack, why, sizeof why)) {
    if (s->cmd) printf("  " FG_GREEN "ok" RESET "\n");
    cur_st++;
    save_progress();
    show_step();
    take_snapshot(stack);
  } else {
    fails++;
    printf("  " FG_YELLOW "not yet:" RESET " %s.", why);
    if (fails == 1) printf(" Type the line as shown, or try your own way; skip moves on, q leaves the tutor.\n");
    else printf(" retry puts the stack back as this step found it; skip moves on.\n");
    if (fails >= 2 && s->cmd) printf("  type:  %s\n", s->cmd);
  }
}

bool tutor_command(const char *line, Stack *stack) {
  if (!active) return false;
  const char *p = line; while (*p == ' ' || *p == '\t') p++;
  char w[32]; int n = 0;
  while (p[n] && !isspace((unsigned char)p[n]) && n < 31) { w[n] = p[n]; n++; }
  w[n] = '\0';
  const char *rest = p + n; while (*rest == ' ') rest++;
  if (*rest) return false;                     /* tutor commands stand alone on the line */
  if (strcmp(w, "q") && strcmp(w, "quit") && strcmp(w, "skip") && strcmp(w, "next") && strcmp(w, "back") &&
      strcmp(w, "again") && strcmp(w, "retry")) return false;
  handled_cmd = true;
  if (!strcmp(w, "retry")) {
    if (have_snap) copy_stack(stack, &snap);
    fails = 0;
    return true;                               /* the stack prints, so you see where you are */
  }
  if (!strcmp(w, "q") || !strcmp(w, "quit")) {
    active = false;
    printf("  Leaving the tutor at %s; tutor comes back here, tutor 1 starts over.\n", tutor_progress);
    skip_stack_printing = true;
    return true;
  }
  if (!strcmp(w, "skip") || !strcmp(w, "next")) {
    const Chapter *c = &chapters[cur_ch];
    if (cur_st < c->n) { cur_st++; save_progress(); show_step(); take_snapshot(stack); }
    else if (cur_ch + 1 < n_chapters) start_chapter(cur_ch + 1, stack);
    else { active = false; snprintf(tutor_progress, sizeof tutor_progress, "done"); }
    skip_stack_printing = true;
    return true;
  }
  if (!strcmp(w, "back")) {
    if (cur_st > 0) cur_st--;
    else if (cur_ch > 0) { cur_ch--; cur_st = chapters[cur_ch].n ? chapters[cur_ch].n - 1 : 0; }
    save_progress();
    if (cur_st == 0) rule(chapters[cur_ch].title, cur_ch);
    const Step *s = &steps[chapters[cur_ch].first + cur_st];
    if (s->prose[0]) printf("\n%s", s->prose);
    if (s->cmd) printf("\n  " BOLD "type:" RESET "  %s\n", s->cmd);
    take_snapshot(stack);
    skip_stack_printing = true;
    return true;
  }
  if (!strcmp(w, "again")) {
    if (cur_st == 0) rule(chapters[cur_ch].title, cur_ch);
    show_step();
    skip_stack_printing = true;
    return true;
  }
  return false;
}

const char *tutor_prompt(void) {
  static char buf[48];
  if (!active) return NULL;
  snprintf(buf, sizeof buf, "tutor %d.%d>> ", cur_ch + 1, cur_st + 1);
  return buf;
}

static int chapter_from(const char *arg) {
  if (!arg || !*arg) return -1;
  char *end; long n = strtol(arg, &end, 10);
  if (end != arg && (*end == '\0' || *end == '.' || *end == ' ')) return (n >= 1 && n <= n_chapters) ? (int)n - 1 : -2;
  for (int i = 0; i < n_chapters; i++) if (strcasecmp(chapters[i].title, arg) == 0) return i;
  for (int i = 0; i < n_chapters; i++) if (strncasecmp(chapters[i].title, arg, strlen(arg)) == 0) return i;
  for (int i = 0; i < n_chapters; i++) {
    const char *t = chapters[i].title;
    size_t n2 = strlen(arg);
    for (const char *q = t; *q; q++) if (strncasecmp(q, arg, n2) == 0) return i;
  }
  return -2;
}

static void list_chapters(void) {
  int pc = -1, ps = -1;
  if (tutor_progress[0] && strcmp(tutor_progress, "done") != 0) sscanf(tutor_progress, "%d.%d", &pc, &ps);
  printf("\n" BOLD "the tutorial" RESET "   tutor <n> opens a chapter, examples <n> prints it\n");
  for (int i = 0; i < n_chapters; i++) {
    const char *mark = "  ";
    if (strcmp(tutor_progress, "done") == 0 || (pc > 0 && i + 1 < pc)) mark = "* ";
    else if (pc == i + 1) mark = "> ";
    printf("  %s%2d  %-40s %d steps\n", mark, i + 1, chapters[i].title, chapters[i].n);
  }
  if (pc > 0) printf("  > is where you are (%s), * what you have been through\n", tutor_progress);
  printf("\n");
}

void tutor_line(const char *args, Stack *stack) {
  load_book();
  skip_stack_printing = true;
  handled_cmd = true;                          /* this line opens or steers the tutor; it is not an answer */
  if (n_chapters == 0) { mm_error("tutor: the book is empty"); return; }
  char a[128] = "";
  if (args) { while (*args == ' ' || *args == '\t') args++; snprintf(a, sizeof a, "%s", args);
    for (char *e = a + strlen(a); e > a && isspace((unsigned char)e[-1]); e--) e[-1] = '\0'; }
  if (!strcmp(a, "list") || !strcmp(a, "chapters")) { list_chapters(); return; }
  if (!strcmp(a, "reset")) { tutor_progress[0] = '\0'; active = false; printf("  Progress forgotten; tutor starts from chapter 1.\n"); return; }
  if (!strcmp(a, "help") || !strcmp(a, "?")) {
    printf("  tutor            the lesson you are on (or chapter 1)       tutor list    the chapters\n"
           "  tutor <n>        open chapter n (also: tutor finance)         tutor reset   forget the progress\n"
           "  in a lesson:     type the line shown (or your own); skip moves on, back returns, again repeats\n"
           "                   the passage, retry restores the stack, q leaves the tutor.  examples <n> prints a chapter.\n");
    return;
  }
  int ch;
  if (a[0]) {
    ch = chapter_from(a);
    if (ch < 0) { mm_error("tutor: no chapter '%s' (tutor list shows them)", a); return; }
    start_chapter(ch, stack);
    return;
  }
  int pc = 0, ps = 0;
  if (strcmp(tutor_progress, "done") == 0) { list_chapters(); printf("  You have been through the book; tutor <n> reopens a chapter.\n"); return; }
  if (sscanf(tutor_progress, "%d.%d", &pc, &ps) == 2 && pc >= 1 && pc <= n_chapters) {
    cur_ch = pc - 1; cur_st = ps - 1; active = true;
    if (cur_st < 0) cur_st = 0;
    if (cur_st > chapters[cur_ch].n) cur_st = chapters[cur_ch].n;
    if (cur_st == 0) show_step();
    else { printf("\n  Back in chapter %d, %s, step %d.\n", cur_ch + 1, chapters[cur_ch].title, cur_st + 1); show_step(); }
    take_snapshot(stack);
    return;
  }
  printf("\n  Welcome. Each step shows a passage and a line to type; type it (or anything you like) and the\n"
         "  tutor looks at the stack. skip moves on, back returns, q leaves; tutor brings you back here.\n");
  start_chapter(0, stack);
}

int tutor_titles(char **out, int cap) {
  load_book();
  int n = 0;
  for (int i = 0; i < n_chapters && n < cap; i++) {
    char buf[64]; snprintf(buf, sizeof buf, "%s", chapters[i].title);
    for (char *q = buf; *q; q++) *q = (char)tolower((unsigned char)*q);
    char *w = buf, *sp;                                     /* the first telling word: "stack", "finance" */
    while ((sp = strchr(w, ' ')) && (!strncmp(w, "the ", 4) || !strncmp(w, "your ", 5) || !strncmp(w, "own ", 4) || !strncmp(w, "a ", 2))) w = sp + 1;
    if ((sp = strchr(w, ' '))) *sp = '\0';
    if ((sp = strchr(w, ','))) *sp = '\0';
    if (w != buf) memmove(buf, w, strlen(w) + 1);
    bool dup = false;
    for (int k = 0; k < n; k++) if (strcmp(out[k], buf) == 0) dup = true;
    if (!dup) out[n++] = xstrdup(buf);
  }
  return n;
}

/* ---- the cookbook ---------------------------------------------------------- */

static void print_chapter(int ch) {
  const Chapter *c = &chapters[ch];
  rule(c->title, ch);
  for (int i = 0; i < c->n; i++) {
    const Step *s = &steps[c->first + i];
    if (s->prose[0]) printf("\n%s", s->prose);
    if (s->cmd) {
      const char *w = s->want;
      char note[128] = "";
      int nr = 0, nc = 0;
      if (!strcmp(w, "any") || !strncmp(w, "depth", 5)) note[0] = '\0';
      else if (!strcmp(w, "empty")) snprintf(note, sizeof note, "(the stack is empty)");
      else if (!strncmp(w, "word ", 5)) snprintf(note, sizeof note, "(defines %s)", w + 5);
      else if (!strcmp(w, "complex")) snprintf(note, sizeof note, "-> a complex number");
      else if (!strcmp(w, "string")) snprintf(note, sizeof note, "-> a string");
      else if (!strncmp(w, "matrix", 6)) {
        if (sscanf(w + 6, " %dx%d", &nr, &nc) == 2) snprintf(note, sizeof note, "-> a matrix, %d x %d", nr, nc);
        else snprintf(note, sizeof note, "-> a matrix");
      }
      else if (!strncmp(w, "near ", 5)) { double t = 0, d = 0; sscanf(w + 5, "%lf %lf", &t, &d); snprintf(note, sizeof note, "-> about %g", t); }
      else snprintf(note, sizeof note, "-> %s", w);
      if (note[0]) printf("\n    %-40s %s\n", s->cmd, note);
      else printf("\n    %s\n", s->cmd);
    }
  }
  printf("\n");
}

void examples_line(const char *args) {
  load_book();
  skip_stack_printing = true;
  handled_cmd = true;
  char a[128] = "";
  if (args) { while (*args == ' ' || *args == '\t') args++; snprintf(a, sizeof a, "%s", args);
    for (char *e = a + strlen(a); e > a && isspace((unsigned char)e[-1]); e--) e[-1] = '\0'; }
  if (!a[0] || !strcmp(a, "list")) {
    printf("\n" BOLD "examples" RESET "   examples <n> or examples <title> prints a chapter of the tutorial as a cookbook;\n"
           "           tutor <n> walks through it step by step\n");
    for (int i = 0; i < n_chapters; i++) printf("  %2d  %s\n", i + 1, chapters[i].title);
    printf("\n");
    return;
  }
  if (!strcmp(a, "all")) { for (int i = 0; i < n_chapters; i++) print_chapter(i); return; }
  int ch = chapter_from(a);
  if (ch < 0) { mm_error("examples: no chapter '%s' (examples lists them)", a); return; }
  print_chapter(ch);
}

/* ---- --tutor-check -------------------------------------------------------------- */

int tutor_self_check(bool (*run)(Stack *stack, char *line)) {
  load_book();
  int bad = 0;
  for (int ch = 0; ch < n_chapters; ch++) {
    Stack s; init_stack(&s);
    const Chapter *c = &chapters[ch];
    for (int i = 0; i < c->n; i++) {
      const Step *st = &steps[c->first + i];
      if (st->cmd) {
        char line[1024]; snprintf(line, sizeof line, "%s", st->cmd);
        long errs = error_count;
        run(&s, line);
        if (error_count != errs) { printf("chapter %d step %d: '%s' reported an error\n", ch + 1, i + 1, st->cmd); bad++; }
      }
      char why[256];
      if (!satisfied(st->want, &s, why, sizeof why)) {
        printf("chapter %d step %d: '%s' -> %s (expected %s)\n", ch + 1, i + 1, st->cmd ? st->cmd : "", why, st->want);
        bad++;
      }
    }
    free_stack(&s);
  }
  printf("tutor: %d chapters, %d steps, %d problem%s\n", n_chapters, n_steps, bad, bad == 1 ? "" : "s");
  return bad ? 1 : 0;
}
