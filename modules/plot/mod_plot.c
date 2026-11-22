/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The plot module: graphs in the terminal, the way the HP-41 drew them on
 * its printer, but with axes, ticks, a legend and colour, and an SVG export
 * for the document the graph is really for.
 *
 * A figure holds up to eight series. The plot words add a series and draw
 * the figure; a new plot replaces the figure unless `hold` is on. The
 * picture is 2 x 2 pixels to a terminal cell (quadrant blocks), sized from
 * the terminal unless psize says otherwise; ticks are "nice" numbers, log
 * axes have decade ticks. psvg writes the same figure as vector graphics. */

#include "module.h"
#include "errors.h"
#include "globals.h"
#include "ops.h"
#include "stack.h"
#include "xt.h"
#include <gsl/gsl_matrix.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* the figure */

enum { K_LINE, K_POINTS, K_STEPS, K_BARS };
enum { MAX_SERIES = 8 };

typedef struct { double *x, *y; size_t n; int kind; char name[32]; } Series;

typedef struct {
  Series s[MAX_SERIES]; int ns;
  char title[80], xlabel[40], ylabel[40];
  int logx, logy, grid;
  int fixed_x, fixed_y; double x0, x1, y0, y1;   /* pxrange / pyrange */
  int W, H;                                       /* cells; 0: from the terminal */
  int hold;
} Figure;

static Figure fig;
static char *last_text;                           /* the last rendering, for ptxt */

static void free_series(void) {
  for (int i = 0; i < fig.ns; i++) { free(fig.s[i].x); free(fig.s[i].y); }
  fig.ns = 0;
}

/* ------------------------------------------------------------------ */
/* helpers */

static int is_vector(const stack_element *e) {
  return e->type == TYPE_MATRIX_REAL && (e->matrix_real->size1 == 1 || e->matrix_real->size2 == 1);
}

static int vector_at(const Stack *s, int depth, const char *what, double **out, size_t *n) {
  const stack_element *e = &s->items[s->top - depth];
  if (!is_vector(e)) return mm_error("%s: needs a real vector (an N x 1 or 1 x N matrix)", what);
  const gsl_matrix *m = e->matrix_real;
  size_t N = m->size1 * m->size2;
  double *v = malloc(N * sizeof *v);
  if (!v) return mm_error("%s: out of memory", what);
  for (size_t i = 0; i < N; i++) v[i] = m->size2 == 1 ? gsl_matrix_get(m, i, 0) : gsl_matrix_get(m, 0, i);
  *out = v; *n = N;
  return 0;
}

static void drop_n(Stack *s, int n) {
  while (n-- > 0) { stack_element e = pop(s); stack_element_free(&e); }
}

static int pop_string(Stack *s, const char *what, char *out, size_t cap) {
  if (s->items[s->top].type != TYPE_STRING) return mm_error("%s: needs a string", what);
  snprintf(out, cap, "%s", s->items[s->top].string);
  drop_n(s, 1);
  return 0;
}

static int add_series(double *x, double *y, size_t n, int kind, const char *name) {
  if (!fig.hold) { free_series(); }
  if (fig.ns == MAX_SERIES) { free(x); free(y); return mm_error("plot: a figure holds at most %d series", MAX_SERIES); }
  Series *sr = &fig.s[fig.ns++];
  sr->x = x; sr->y = y; sr->n = n; sr->kind = kind;
  snprintf(sr->name, sizeof sr->name, "%s", name ? name : "");
  return 0;
}

/* "nice" tick step for a range, aiming at about `want` ticks */
static double nice_step(double range, int want) {
  if (range <= 0 || !isfinite(range)) return 1;
  double raw = range / want, mag = pow(10, floor(log10(raw))), r = raw / mag;
  double step = r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10;
  return step * mag;
}

/* format a tick value compactly */
static void fmt_tick(double v, char *out, size_t cap) {
  if (v == 0) { snprintf(out, cap, "0"); return; }
  double av = fabs(v);
  if (av >= 1e6 || av < 1e-3) snprintf(out, cap, "%.2g", v);
  else { snprintf(out, cap, "%.6g", v); }
}

/* ------------------------------------------------------------------ */
/* the canvas: cells of 2 x 2 pixels, a series index per pixel */

typedef struct {
  int W, H;                     /* plot area in cells */
  int px, py;                   /* pixels: 2W x 2H */
  unsigned char *pix;           /* 0: empty, else series+1 */
  double xa, xb, ya, yb;        /* axis range (transformed if log) */
} Canvas;

static double tx(double x) { return fig.logx ? log10(x) : x; }
static double ty(double y) { return fig.logy ? log10(y) : y; }

static void set_px(Canvas *c, int x, int y, int s) {
  if (x < 0 || y < 0 || x >= c->px || y >= c->py) return;
  c->pix[(c->py - 1 - y) * c->px + x] = (unsigned char)(s + 1);
}

static void line_px(Canvas *c, int x0, int y0, int x1, int y1, int s) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    set_px(c, x0, y0, s);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static int to_px(double v, double a, double b, int n) {
  double t = (v - a) / (b - a);
  int p = (int)floor(t * (n - 1) + 0.5);
  return p;
}

/* the extent of the data on each axis, after the log transform, with bars from 0 */
static int data_range(double *xa, double *xb, double *ya, double *yb) {
  int any = 0;
  *xa = *ya = INFINITY; *xb = *yb = -INFINITY;
  for (int i = 0; i < fig.ns; i++) {
    const Series *sr = &fig.s[i];
    for (size_t k = 0; k < sr->n; k++) {
      double x = sr->x[k], y = sr->y[k];
      if (!isfinite(x) || !isfinite(y)) continue;
      if (fig.logx && x <= 0) continue;
      if (fig.logy && y <= 0) continue;
      x = fig.logx ? log10(x) : x; y = fig.logy ? log10(y) : y;
      if (x < *xa) *xa = x;
      if (x > *xb) *xb = x;
      if (y < *ya) *ya = y;
      if (y > *yb) *yb = y;
      any = 1;
    }
    if (sr->kind == K_BARS && !fig.logy) { if (*ya > 0) *ya = 0; if (*yb < 0) *yb = 0; }
  }
  return any;
}

/* expand a range to tick multiples; a degenerate range gets a width */
static void expand(double *a, double *b, double *step, int want, int is_log) {
  if (*b - *a < 1e-12 * fmax(fabs(*a), 1)) { *a -= 1; *b += 1; }
  if (is_log) { *a = floor(*a); *b = ceil(*b); *step = fmax(1, ceil((*b - *a) / want)); return; }
  *step = nice_step(*b - *a, want);
  *a = floor(*a / *step) * *step;
  *b = ceil(*b / *step) * *step;
  if (*b - *a < *step) *b = *a + *step;
}

/* ------------------------------------------------------------------ */
/* drawing to text */

static const int colours[MAX_SERIES] = { 39, 208, 46, 199, 226, 51, 213, 244 };   /* blue orange green pink yellow cyan violet grey */

static void terminal_size(int *cols, int *rows) {
  struct winsize w;
  *cols = 80; *rows = 24;
  if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 20 && w.ws_row > 8) { *cols = w.ws_col; *rows = w.ws_row; }
}

/* append to a growing string */
typedef struct { char *s; size_t n, cap; } Buf;
static void bput(Buf *b, const char *t) {
  size_t l = strlen(t);
  if (b->n + l + 1 > b->cap) { b->cap = (b->n + l + 1) * 2; b->s = realloc(b->s, b->cap); }
  memcpy(b->s + b->n, t, l + 1); b->n += l;
}
static void bputf(Buf *b, const char *fmt, ...) {
  char t[512]; va_list ap; va_start(ap, fmt); vsnprintf(t, sizeof t, fmt, ap); va_end(ap); bput(b, t);
}

static int render(Buf *out, int colour) {
  int cols, rows; terminal_size(&cols, &rows);
  int W = fig.W > 0 ? fig.W : (cols - 12 > 100 ? 100 : cols - 12);
  int H = fig.H > 0 ? fig.H : (rows - 6 > 30 ? 30 : rows - 6);
  if (W < 10) W = 10;
  if (H < 4) H = 4;
  Canvas c = { W, H, 2 * W, 2 * H, calloc((size_t)(4 * W * H), 1), 0, 0, 0, 0 };
  if (!c.pix) return mm_error("plot: out of memory");
  double xa, xb, ya, yb, xs, ys;
  if (!data_range(&xa, &xb, &ya, &yb)) { free(c.pix); return mm_error("plot: nothing to draw%s", fig.logx || fig.logy ? " (log axes need positive values)" : ""); }
  if (fig.fixed_x) { xa = tx(fig.x0); xb = tx(fig.x1); }
  if (fig.fixed_y) { ya = ty(fig.y0); yb = ty(fig.y1); }
  expand(&xa, &xb, &xs, 6, fig.logx);
  expand(&ya, &yb, &ys, 5, fig.logy);
  if (fig.fixed_x) { xa = tx(fig.x0); xb = tx(fig.x1); }
  if (fig.fixed_y) { ya = ty(fig.y0); yb = ty(fig.y1); }
  c.xa = xa; c.xb = xb; c.ya = ya; c.yb = yb;

  /* the series */
  for (int i = 0; i < fig.ns; i++) {
    const Series *sr = &fig.s[i];
    int px = -1, py = -1, have = 0;
    for (size_t k = 0; k < sr->n; k++) {
      double x = sr->x[k], y = sr->y[k];
      if (!isfinite(x) || !isfinite(y) || (fig.logx && x <= 0) || (fig.logy && y <= 0)) { have = 0; continue; }
      int X = to_px(tx(x), xa, xb, c.px), Y = to_px(ty(y), ya, yb, c.py);
      switch (sr->kind) {
      case K_LINE:   if (have) line_px(&c, px, py, X, Y, i); else set_px(&c, X, Y, i); break;
      case K_POINTS: set_px(&c, X, Y, i); set_px(&c, X + 1, Y, i); set_px(&c, X, Y + 1, i); set_px(&c, X + 1, Y + 1, i); break;
      case K_STEPS:  if (have) { line_px(&c, px, py, X, py, i); line_px(&c, X, py, X, Y, i); } else set_px(&c, X, Y, i); break;
      case K_BARS: {
        int Y0 = to_px(fig.logy ? ya : 0, ya, yb, c.py);
        int half = sr->n > 1 ? (int)fmax(1, floor(0.4 * c.px / (double)(sr->n) )) : 2;
        for (int d = -half; d <= half; d++) line_px(&c, X + d, Y0, X + d, Y, i);
        break;
      }
      }
      px = X; py = Y; have = 1;
    }
  }

  /* the text */
  static const char *const quad[16] = { " ", "▘", "▝", "▀", "▖", "▌", "▞", "▛", "▗", "▚", "▐", "▜", "▄", "▙", "▟", "█" };
  int lw = 9;                                   /* width of the y-label column */
  bput(out, "\n");
  if (fig.title[0]) bputf(out, "%*s%s\n", lw + 1 + (W - (int)strlen(fig.title)) / 2, "", fig.title);
  /* legend */
  int named = 0; for (int i = 0; i < fig.ns; i++) if (fig.s[i].name[0]) named = 1;
  if (named) {
    bputf(out, "%*s", lw + 1, "");
    for (int i = 0; i < fig.ns; i++) {
      if (colour) bputf(out, "\033[38;5;%dm", colours[i]);
      bputf(out, "%s %s", fig.s[i].kind == K_POINTS ? "••" : fig.s[i].kind == K_BARS ? "▇▇" : "▬▬", fig.s[i].name[0] ? fig.s[i].name : "");
      if (colour) bput(out, "\033[0m");
      bput(out, "   ");
    }
    bput(out, "\n");
  }
  /* which rows and columns a tick falls in: each cell spans two pixels plus half a pixel each side, contiguous with its neighbours */
  unsigned char *rowtick = calloc((size_t)H, 1), *coltick = calloc((size_t)W, 1);
  double hpx = (yb - ya) / (c.py - 1), wpx = (xb - xa) / (c.px - 1);
  for (int r = 0; r < H; r++) {
    double y_top = yb - hpx * (2.0 * r) + hpx / 2, y_bot = yb - hpx * (2.0 * r + 1) - hpx / 2, eps = hpx * 1e-6;
    double t = ceil((y_bot - eps) / ys) * ys;
    rowtick[r] = t < y_top - eps && t >= y_bot - eps;
  }
  for (int col = 0; col < W; col++) {
    double x_l = xa + wpx * (2.0 * col) - wpx / 2, x_r = xa + wpx * (2.0 * col + 1) + wpx / 2, eps = wpx * 1e-6;
    double t = ceil((x_l - eps) / xs) * xs;
    coltick[col] = t < x_r - eps && t >= x_l - eps;
  }
  for (int r = 0; r < H; r++) {
    double y_bot = yb - hpx * (2.0 * r + 1) - hpx / 2;
    char lab[32] = "";
    if (rowtick[r]) {
      double t = ceil((y_bot - hpx * 1e-6) / ys) * ys;
      if (fig.logy) snprintf(lab, sizeof lab, "1e%d", (int)lrint(t)); else fmt_tick(t, lab, sizeof lab);
    }
    if (r == H - 1 && !lab[0]) { if (fig.logy) snprintf(lab, sizeof lab, "1e%d", (int)lrint(ya)); else fmt_tick(ya, lab, sizeof lab); }
    if (r == 0 && !lab[0]) { if (fig.logy) snprintf(lab, sizeof lab, "1e%d", (int)lrint(yb)); else fmt_tick(yb, lab, sizeof lab); }
    bputf(out, "%*s %s", lw - 1, lab, lab[0] ? "┤" : "│");
    int last = -1;
    for (int col = 0; col < W; col++) {
      int x = 2 * col, y = 2 * r;
      unsigned char p[4] = { c.pix[y * c.px + x], c.pix[y * c.px + x + 1], c.pix[(y + 1) * c.px + x], c.pix[(y + 1) * c.px + x + 1] };
      int v = (p[0] ? 1 : 0) | (p[1] ? 2 : 0) | (p[2] ? 4 : 0) | (p[3] ? 8 : 0);
      int sidx = -1; for (int k = 0; k < 4; k++) if (p[k]) sidx = p[k] - 1;
      if (v == 0 && fig.grid && (rowtick[r] || coltick[col])) {
        if (colour && last != -2) { bput(out, "\033[38;5;240m"); last = -2; }
        bput(out, rowtick[r] && coltick[col] ? "┼" : coltick[col] ? "┊" : "┈");
        continue;
      }
      if (colour && sidx >= 0 && sidx != last) { bputf(out, "\033[38;5;%dm", colours[sidx]); last = sidx; }
      if (colour && sidx < 0 && last != -1) { bput(out, "\033[0m"); last = -1; }
      bput(out, quad[v]);
    }
    if (colour && last != -1) bput(out, "\033[0m");
    bput(out, "\n");
  }
  /* x axis */
  bputf(out, "%*s└", lw, "");
  for (int col = 0; col < W; col++) bput(out, coltick[col] ? "┬" : "─");
  bput(out, "\n");
  free(rowtick); free(coltick);
  /* x tick labels */
  char *row = calloc((size_t)W + 40, 1); memset(row, ' ', (size_t)W + 39);
  for (long k = lrint(floor(xa / xs)); k * xs <= xb + 1e-9 * xs; k++) {
    double t = k * xs;
    if (t < xa - 1e-9 * xs) continue;
    int col = to_px(t, xa, xb, c.px) / 2;
    char lab[32];
    if (fig.logx) snprintf(lab, sizeof lab, "1e%d", (int)lrint(t)); else fmt_tick(t, lab, sizeof lab);
    int start = col - (int)strlen(lab) / 2; if (start < 0) start = 0;
    if (start + (int)strlen(lab) > W + 8) start = W + 8 - (int)strlen(lab);
    int clash = 0; for (int q = (start > 0 ? start - 1 : 0); q < start + (int)strlen(lab) + 1 && q < W + 39; q++) if (row[q] != ' ') clash = 1;
    if (!clash) memcpy(row + start, lab, strlen(lab));
  }
  for (int k = W + 38; k > 0 && row[k] == ' '; k--) row[k] = '\0';
  bputf(out, "%*s %s\n", lw, "", row);
  free(row);
  if (fig.xlabel[0] || fig.ylabel[0]) bputf(out, "%*s%s%s%s\n", lw + 1, "", fig.ylabel[0] ? fig.ylabel : "", fig.ylabel[0] && fig.xlabel[0] ? "  vs  " : "", fig.xlabel[0] ? fig.xlabel : "");
  free(c.pix);
  return 0;
}

static int draw(void) {
  Buf b = { NULL, 0, 0 };
  int colour = isatty(STDOUT_FILENO);
  if (render(&b, colour) != 0) { free(b.s); return -1; }
  fputs(b.s, stdout);
  fflush(stdout);
  free(last_text);
  Buf p = { NULL, 0, 0 };
  render(&p, 0);
  last_text = p.s;
  free(b.s);
  return 0;
}

/* ------------------------------------------------------------------ */
/* the plot words */

static int plot_xy(Stack *s, const char *what, int kind) {
  double *x, *y; size_t nx, ny;
  if (vector_at(s, 1, what, &x, &nx) != 0) return -1;
  if (vector_at(s, 0, what, &y, &ny) != 0) { free(x); return -1; }
  if (nx != ny || nx == 0) { free(x); free(y); return mm_error("%s: x and y must be vectors of the same length", what); }
  drop_n(s, 2);
  if (add_series(x, y, nx, kind, NULL) != 0) return -1;
  return draw();
}
static int op_plot(Stack *s)    { return plot_xy(s, "plot", K_LINE); }
static int op_scatter(Stack *s) { return plot_xy(s, "scatter", K_POINTS); }
static int op_steps(Stack *s)   { return plot_xy(s, "steps", K_STEPS); }
static int op_bars(Stack *s)    { return plot_xy(s, "bars", K_BARS); }

/* M -- : column 0 is x, the others are series */
static int op_mplot(Stack *s) {
  const stack_element *e = &s->items[s->top];
  if (e->type != TYPE_MATRIX_REAL || e->matrix_real->size2 < 2) return mm_error("mplot: needs a real matrix with x in the first column and at least one more");
  const gsl_matrix *m = e->matrix_real;
  size_t n = m->size1, cols = m->size2;
  if (cols - 1 > MAX_SERIES) return mm_error("mplot: at most %d series", MAX_SERIES);
  int hold0 = fig.hold;
  if (!fig.hold) free_series();
  fig.hold = 1;
  for (size_t j = 1; j < cols; j++) {
    double *x = malloc(n * sizeof *x), *y = malloc(n * sizeof *y);
    for (size_t i = 0; i < n; i++) { x[i] = gsl_matrix_get(m, i, 0); y[i] = gsl_matrix_get(m, i, j); }
    char name[16] = "";
    if (cols > 2) snprintf(name, sizeof name, "col %zu", j);
    if (add_series(x, y, n, K_LINE, cols > 2 ? name : NULL) != 0) { fig.hold = hold0; return -1; }
  }
  fig.hold = hold0;
  drop_n(s, 1);
  return draw();
}

/* a b xt -- : the word over [a, b], one point per pixel column; errors are gaps */
static int op_fplot(Stack *s) {
  double xt, a, b;
  if (pop_real_arg(s, "fplot", &xt) != 0) return -1;
  if (xt_check(xt) != 0) return -1;
  if (pop_two_reals(s, "fplot", &a, &b) != 0) return -1;
  if (!(b > a) || !isfinite(a) || !isfinite(b)) return mm_error("fplot: needs a < b");
  int cols, rows; terminal_size(&cols, &rows);
  int W = fig.W > 0 ? fig.W : (cols - 12 > 100 ? 100 : cols - 12);
  size_t n = (size_t)(2 * W);
  double *x = malloc(n * sizeof *x), *y = malloc(n * sizeof *y);
  int depth0 = s->top + 1;
  bool quiet = errors_quiet; errors_quiet = true;
  for (size_t i = 0; i < n; i++) {
    x[i] = fig.logx ? pow(10, log10(a) + (log10(b) - log10(a)) * (double)i / (double)(n - 1)) : a + (b - a) * (double)i / (double)(n - 1);
    long errs = error_count;
    push_real(s, x[i]);
    xt_exec(s, xt);
    if (error_count != errs || s->top + 1 != depth0 + 1 || s->items[s->top].type != TYPE_REAL) {
      y[i] = NAN;
      while (s->top + 1 > depth0) { stack_element e = pop(s); stack_element_free(&e); }
      continue;
    }
    y[i] = s->items[s->top].real;
    stack_element e = pop(s); stack_element_free(&e);
  }
  errors_quiet = quiet;
  if (add_series(x, y, n, K_LINE, xt_name(xt)) != 0) return -1;
  return draw();
}

/* v k -- : histogram with k bins */
static int op_hplot(Stack *s) {
  double k;
  if (pop_real_arg(s, "hplot", &k) != 0) return -1;
  if (k < 1 || k > 1000 || k != floor(k)) return mm_error("hplot: needs 1 to 1000 bins");
  double *v; size_t n;
  if (vector_at(s, 0, "hplot", &v, &n) != 0) return -1;
  double lo = INFINITY, hi = -INFINITY;
  for (size_t i = 0; i < n; i++) if (isfinite(v[i])) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
  if (!(hi > lo)) { free(v); return mm_error("hplot: the data have no spread"); }
  size_t K = (size_t)k;
  double *x = malloc(K * sizeof *x), *h = calloc(K, sizeof *h), w = (hi - lo) / (double)K;
  for (size_t j = 0; j < K; j++) x[j] = lo + (j + 0.5) * w;
  for (size_t i = 0; i < n; i++) if (isfinite(v[i])) { size_t j = (size_t)((v[i] - lo) / w); if (j >= K) j = K - 1; h[j] += 1; }
  free(v);
  drop_n(s, 1);
  if (add_series(x, h, K, K_BARS, NULL) != 0) return -1;
  return draw();
}

static int op_hold(Stack *s)   { (void)s; fig.hold = 1; return 0; }
static int op_nohold(Stack *s) { (void)s; fig.hold = 0; return 0; }
static int op_show(Stack *s)   { (void)s; if (fig.ns == 0) return mm_error("show: no figure yet"); return draw(); }
static int op_pclear(Stack *s) {
  (void)s; free_series();
  fig.title[0] = fig.xlabel[0] = fig.ylabel[0] = '\0';
  fig.logx = fig.logy = fig.grid = fig.fixed_x = fig.fixed_y = fig.hold = 0;
  return 0;
}
static int op_ptitle(Stack *s)  { return pop_string(s, "ptitle", fig.title, sizeof fig.title); }
static int op_pxlabel(Stack *s) { return pop_string(s, "pxlabel", fig.xlabel, sizeof fig.xlabel); }
static int op_pylabel(Stack *s) { return pop_string(s, "pylabel", fig.ylabel, sizeof fig.ylabel); }
static int op_plegend(Stack *s) {
  char t[256];
  if (pop_string(s, "plegend", t, sizeof t) != 0) return -1;
  int i = 0;
  for (char *p = strtok(t, ","); p && i < MAX_SERIES; p = strtok(NULL, ","), i++) {
    while (*p == ' ') p++;
    if (i < fig.ns) snprintf(fig.s[i].name, sizeof fig.s[i].name, "%s", p);
  }
  return 0;
}
static int op_pxlog(Stack *s) { (void)s; fig.logx = 1; return 0; }
static int op_pylog(Stack *s) { (void)s; fig.logy = 1; return 0; }
static int op_plin(Stack *s)  { (void)s; fig.logx = fig.logy = 0; return 0; }
static int op_pgrid(Stack *s) { (void)s; fig.grid = !fig.grid; return 0; }
static int op_pauto(Stack *s) { (void)s; fig.fixed_x = fig.fixed_y = 0; return 0; }
static int op_pxrange(Stack *s) {
  double a, b; if (pop_two_reals(s, "pxrange", &a, &b) != 0) return -1;
  if (!(b > a)) return mm_error("pxrange: needs a < b");
  fig.fixed_x = 1; fig.x0 = a; fig.x1 = b; return 0;
}
static int op_pyrange(Stack *s) {
  double a, b; if (pop_two_reals(s, "pyrange", &a, &b) != 0) return -1;
  if (!(b > a)) return mm_error("pyrange: needs a < b");
  fig.fixed_y = 1; fig.y0 = a; fig.y1 = b; return 0;
}
static int op_psize(Stack *s) {
  double w, h; if (pop_two_reals(s, "psize", &w, &h) != 0) return -1;
  if (w != 0 && (w < 10 || w > 400)) return mm_error("psize: width 10 to 400 cells, or 0 for the terminal's");
  if (h != 0 && (h < 4 || h > 200)) return mm_error("psize: height 4 to 200 rows, or 0 for the terminal's");
  fig.W = (int)w; fig.H = (int)h; return 0;
}

/* "file" -- : the last rendering as text */
static int op_ptxt(Stack *s) {
  char path[4096];
  if (pop_string(s, "ptxt", path, sizeof path) != 0) return -1;
  if (!last_text) return mm_error("ptxt: no figure yet");
  FILE *f = fopen(path, "w");
  if (!f) return mm_error("ptxt: cannot write %s", path);
  fputs(last_text, f);
  fclose(f);
  return 0;
}

/* ------------------------------------------------------------------ */
/* SVG */

static const char *const svg_colours[MAX_SERIES] = { "#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f" };

static void svg_escape(FILE *f, const char *t) {
  for (; *t; t++) {
    if (*t == '<') fputs("&lt;", f); else if (*t == '>') fputs("&gt;", f); else if (*t == '&') fputs("&amp;", f); else fputc(*t, f);
  }
}

/* "file" -- : the figure as vector graphics, 640 x 400 */
static int op_psvg(Stack *s) {
  char path[4096];
  if (pop_string(s, "psvg", path, sizeof path) != 0) return -1;
  if (fig.ns == 0) return mm_error("psvg: no figure yet");
  double xa, xb, ya, yb, xs, ys;
  if (!data_range(&xa, &xb, &ya, &yb)) return mm_error("psvg: nothing to draw");
  if (fig.fixed_x) { xa = tx(fig.x0); xb = tx(fig.x1); }
  if (fig.fixed_y) { ya = ty(fig.y0); yb = ty(fig.y1); }
  expand(&xa, &xb, &xs, 8, fig.logx);
  expand(&ya, &yb, &ys, 6, fig.logy);
  if (fig.fixed_x) { xa = tx(fig.x0); xb = tx(fig.x1); }
  if (fig.fixed_y) { ya = ty(fig.y0); yb = ty(fig.y1); }
  const double Wd = 640, Hd = 400, L = 64, R = 20, T = fig.title[0] ? 36 : 16, B = fig.xlabel[0] ? 48 : 32;
  double pw = Wd - L - R, ph = Hd - T - B;
#define SX(x) (L + ((x) - xa) / (xb - xa) * pw)
#define SY(y) (T + (yb - (y)) / (yb - ya) * ph)
  FILE *f = fopen(path, "w");
  if (!f) return mm_error("psvg: cannot write %s", path);
  fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%g\" height=\"%g\" viewBox=\"0 0 %g %g\" font-family=\"Helvetica, Arial, sans-serif\" font-size=\"11\">\n", Wd, Hd, Wd, Hd);
  fprintf(f, "<rect width=\"%g\" height=\"%g\" fill=\"white\"/>\n", Wd, Hd);
  /* grid and ticks */
  for (long k = lrint(floor(xa / xs)); k * xs <= xb + 1e-9 * xs; k++) {
    double t = k * xs; if (t < xa - 1e-9 * xs) continue;
    double X = SX(t); char lab[32];
    if (fig.logx) snprintf(lab, sizeof lab, "1e%d", (int)lrint(t)); else fmt_tick(t, lab, sizeof lab);
    if (fig.grid) fprintf(f, "<line x1=\"%.1f\" y1=\"%g\" x2=\"%.1f\" y2=\"%g\" stroke=\"#ddd\"/>\n", X, T, X, T + ph);
    fprintf(f, "<line x1=\"%.1f\" y1=\"%g\" x2=\"%.1f\" y2=\"%g\" stroke=\"black\"/>\n", X, T + ph, X, T + ph + 4);
    fprintf(f, "<text x=\"%.1f\" y=\"%g\" text-anchor=\"middle\">%s</text>\n", X, T + ph + 16, lab);
  }
  for (long k = lrint(floor(ya / ys)); k * ys <= yb + 1e-9 * ys; k++) {
    double t = k * ys; if (t < ya - 1e-9 * ys) continue;
    double Y = SY(t); char lab[32];
    if (fig.logy) snprintf(lab, sizeof lab, "1e%d", (int)lrint(t)); else fmt_tick(t, lab, sizeof lab);
    if (fig.grid) fprintf(f, "<line x1=\"%g\" y1=\"%.1f\" x2=\"%g\" y2=\"%.1f\" stroke=\"#ddd\"/>\n", L, Y, L + pw, Y);
    fprintf(f, "<line x1=\"%g\" y1=\"%.1f\" x2=\"%g\" y2=\"%.1f\" stroke=\"black\"/>\n", L - 4, Y, L, Y);
    fprintf(f, "<text x=\"%g\" y=\"%.1f\" text-anchor=\"end\" dominant-baseline=\"middle\">%s</text>\n", L - 7, Y, lab);
  }
  fprintf(f, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"none\" stroke=\"black\"/>\n", L, T, pw, ph);
  fprintf(f, "<clipPath id=\"c\"><rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\"/></clipPath>\n", L, T, pw, ph);
  /* series */
  for (int i = 0; i < fig.ns; i++) {
    const Series *sr = &fig.s[i];
    const char *col = svg_colours[i];
    if (sr->kind == K_LINE || sr->kind == K_STEPS) {
      fprintf(f, "<polyline clip-path=\"url(#c)\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.5\" points=\"", col);
      int have = 0; double py = 0;
      for (size_t k = 0; k < sr->n; k++) {
        double x = sr->x[k], y = sr->y[k];
        if (!isfinite(x) || !isfinite(y) || (fig.logx && x <= 0) || (fig.logy && y <= 0)) { if (have) { fprintf(f, "\" /><polyline clip-path=\"url(#c)\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.5\" points=\"", col); have = 0; } continue; }
        double X = SX(tx(x)), Y = SY(ty(y));
        if (sr->kind == K_STEPS && have) fprintf(f, "%.1f,%.1f ", X, py);
        fprintf(f, "%.1f,%.1f ", X, Y);
        py = Y; have = 1;
      }
      fputs("\"/>\n", f);
    } else if (sr->kind == K_POINTS) {
      for (size_t k = 0; k < sr->n; k++) {
        double x = sr->x[k], y = sr->y[k];
        if (!isfinite(x) || !isfinite(y) || (fig.logx && x <= 0) || (fig.logy && y <= 0)) continue;
        fprintf(f, "<circle clip-path=\"url(#c)\" cx=\"%.1f\" cy=\"%.1f\" r=\"2.5\" fill=\"%s\"/>\n", SX(tx(x)), SY(ty(y)), col);
      }
    } else {
      double bw = sr->n > 1 ? 0.8 * pw / (double)sr->n : 20, y0 = SY(fig.logy ? ya : 0);
      for (size_t k = 0; k < sr->n; k++) {
        double x = sr->x[k], y = sr->y[k];
        if (!isfinite(x) || !isfinite(y) || (fig.logy && y <= 0)) continue;
        double X = SX(tx(x)), Y = SY(ty(y));
        fprintf(f, "<rect clip-path=\"url(#c)\" x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" fill=\"%s\" fill-opacity=\"0.8\"/>\n", X - bw / 2, fmin(Y, y0), bw, fabs(y0 - Y), col);
      }
    }
  }
  /* legend, title, labels */
  int named = 0; for (int i = 0; i < fig.ns; i++) if (fig.s[i].name[0]) named = 1;
  if (named) for (int i = 0; i < fig.ns; i++) {
    double Y = T + 14 + 16 * i;
    fprintf(f, "<line x1=\"%g\" y1=\"%.1f\" x2=\"%g\" y2=\"%.1f\" stroke=\"%s\" stroke-width=\"3\"/>\n", L + pw - 100, Y, L + pw - 80, Y, svg_colours[i]);
    fprintf(f, "<text x=\"%g\" y=\"%.1f\" dominant-baseline=\"middle\">", L + pw - 74, Y); svg_escape(f, fig.s[i].name); fputs("</text>\n", f);
  }
  if (fig.title[0]) { fprintf(f, "<text x=\"%g\" y=\"22\" text-anchor=\"middle\" font-size=\"14\" font-weight=\"bold\">", L + pw / 2); svg_escape(f, fig.title); fputs("</text>\n", f); }
  if (fig.xlabel[0]) { fprintf(f, "<text x=\"%g\" y=\"%g\" text-anchor=\"middle\">", L + pw / 2, Hd - 10); svg_escape(f, fig.xlabel); fputs("</text>\n", f); }
  if (fig.ylabel[0]) { fprintf(f, "<text transform=\"translate(14,%g) rotate(-90)\" text-anchor=\"middle\">", T + ph / 2); svg_escape(f, fig.ylabel); fputs("</text>\n", f); }
  fputs("</svg>\n", f);
  fclose(f);
#undef SX
#undef SY
  return 0;
}

/* ------------------------------------------------------------------ */

static void plot_shutdown(void) { free_series(); free(last_text); last_text = NULL; }

static const Op table[] = {
  { "plot",    2, op_plot,    "Plot", "x y --",   "draw y against x as a line (vectors of the same length); a new figure unless hold is on", "0 0.1 6.3 rrange dup sin plot" },
  { "scatter", 2, op_scatter, "Plot", "x y --",   "draw y against x as points", "[1 2 3 4 5] [2.1 3.9 6.2 7.8 10.1] scatter" },
  { "steps",   2, op_steps,   "Plot", "x y --",   "draw y against x as a step function (a Kaplan-Meier curve, a cumulative count)", "[0 2 3 5 8] [1 0.833 0.667 0.444 0.222] steps" },
  { "bars",    2, op_bars,    "Plot", "x h --",   "draw bars of height h at x", "[1 2 3 4] [3 7 5 2] bars" },
  { "hplot",   2, op_hplot,   "Plot", "v k --",   "histogram of the data in v with k equal bins", "1000 normrnd 20 hplot" },
  { "mplot",   1, op_mplot,   "Plot", "M --",     "draw every column of M after the first against the first (odeint output, lorenzm, lifetab ...)", ": f swap drop chs ; 1 0 5 50 @f odeint mplot" },
  { "fplot",   3, op_fplot,   "Plot", "a b xt --", "draw the word (x -- y) over [a, b], one point per pixel column; points where it fails are left out", "0 10 @sin fplot" },
  { "hold",    0, op_hold,    "Plot", "--",       "the following plots add to the figure instead of replacing it", "hold" },
  { "nohold",  0, op_nohold,  "Plot", "--",       "the next plot starts a new figure (the default)", "nohold" },
  { "show",    0, op_show,    "Plot", "--",       "draw the figure again (after psize, pgrid, plegend ...)", "show" },
  { "pclear",  0, op_pclear,  "Plot", "--",       "forget the figure, its title, labels, ranges and log axes", "pclear" },
  { "ptitle",  1, op_ptitle,  "Plot", "\"s\" --",  "title above the figure (draw with show)", "\"Survival\" ptitle" },
  { "pxlabel", 1, op_pxlabel, "Plot", "\"s\" --",  "label of the x axis", "\"age\" pxlabel" },
  { "pylabel", 1, op_pylabel, "Plot", "\"s\" --",  "label of the y axis", "\"S(t)\" pylabel" },
  { "plegend", 1, op_plegend, "Plot", "\"s\" --",  "names of the series, comma-separated, in the order drawn", "\"men,women\" plegend" },
  { "pxlog",   0, op_pxlog,   "Plot", "--",       "logarithmic x axis (decade ticks); values <= 0 are left out", "pxlog" },
  { "pylog",   0, op_pylog,   "Plot", "--",       "logarithmic y axis", "pylog" },
  { "plin",    0, op_plin,    "Plot", "--",       "linear axes again", "plin" },
  { "pgrid",   0, op_pgrid,   "Plot", "--",       "toggle the grid at the ticks", "pgrid" },
  { "pxrange", 2, op_pxrange, "Plot", "a b --",   "fix the x axis to [a, b] instead of fitting the data", "0 100 pxrange" },
  { "pyrange", 2, op_pyrange, "Plot", "a b --",   "fix the y axis to [a, b]", "0 1 pyrange" },
  { "pauto",   0, op_pauto,   "Plot", "--",       "axes fitted to the data again", "pauto" },
  { "psize",   2, op_psize,   "Plot", "w h --",   "size of the plot area in cells and rows; 0 0 follows the terminal (the default)", "60 15 psize" },
  { "psvg",    1, op_psvg,    "Plot", "\"file\" --", "write the figure as SVG, 640 x 400, vector lines, for a document", "\"fig.svg\" psvg" },
  { "ptxt",    1, op_ptxt,    "Plot", "\"file\" --", "write the last drawing as text", "\"fig.txt\" ptxt" },
};

const Module mm41_mod_plot = {
  "plot", "1.0", "graphs in the terminal: lines, points, steps, bars, histograms, functions; log axes, legend, SVG export",
  table, (int)(sizeof table / sizeof table[0]), NULL, plot_shutdown, 0
};
