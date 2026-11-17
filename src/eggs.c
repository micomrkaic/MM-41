/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Nothing in this file is documented, listed, completed or described by
   help, and none of it touches the stack. A line is only taken here when
   every word on it is otherwise nothing in MM-41: a command or a user word
   of the same name is left alone. */

#define _POSIX_C_SOURCE 200809L
#include "eggs.h"
#include "ops.h"
#include "skins.h"
#include "words.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- one-liners ---- */

static int say(const char *what) { printf("%s\n", what); return 1; }

/* ---- the machine itself ---- */

static int hp41(void) {
  static const char *const art[] = {
    "        ╔═══════════════════════════════════╗",
    "        ║   ON     USER     PRGM    ALPHA   ║",
    "        ║  ┌──────────────────────────────┐ ║",
    "        ║  │                              │ ║",
    "        ║  │  0.0000                      │ ║",
    "        ║  │  USER      RAD               │ ║",
    "        ║  └──────────────────────────────┘ ║",
    "        ║                                   ║",
    "        ║   ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐   ║",
    "        ║   │Σ+ │ │1/x│ │√x │ │LOG│ │LN │   ║",
    "        ║   └───┘ └───┘ └───┘ └───┘ └───┘   ║",
    "        ║   ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐   ║",
    "        ║   │x≷y│ │R↓ │ │SIN│ │COS│ │TAN│   ║",
    "        ║   └───┘ └───┘ └───┘ └───┘ └───┘   ║",
    "        ║   ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐   ║",
    "        ║   │ ■ │ │XEQ│ │STO│ │RCL│ │SST│   ║",
    "        ║   └───┘ └───┘ └───┘ └───┘ └───┘   ║",
    "        ║   ┌─────────┐ ┌───┐ ┌───┐ ┌───┐   ║",
    "        ║   │ ENTER↑  │ │CHS│ │EEX│ │ ← │   ║",
    "        ║   └─────────┘ └───┘ └───┘ └───┘   ║",
    "        ║   ┌───┐   ┌────┐ ┌────┐ ┌────┐    ║",
    "        ║   │ − │   │ 7  │ │ 8  │ │ 9  │    ║",
    "        ║   └───┘   └────┘ └────┘ └────┘    ║",
    "        ║   ┌───┐   ┌────┐ ┌────┐ ┌────┐    ║",
    "        ║   │ + │   │ 4  │ │ 5  │ │ 6  │    ║",
    "        ║   └───┘   └────┘ └────┘ └────┘    ║",
    "        ║   ┌───┐   ┌────┐ ┌────┐ ┌────┐    ║",
    "        ║   │ × │   │ 1  │ │ 2  │ │ 3  │    ║",
    "        ║   └───┘   └────┘ └────┘ └────┘    ║",
    "        ║   ┌───┐   ┌────┐ ┌────┐ ┌────┐    ║",
    "        ║   │ ÷ │   │ 0  │ │ .  │ │R/S │    ║",
    "        ║   └───┘   └────┘ └────┘ └────┘    ║",
    "        ║                                   ║",
    "        ║            mm-41                  ║",
    "        ╚═══════════════════════════════════╝",
  };
  putchar('\n');
  for (size_t i = 0; i < sizeof art / sizeof art[0]; i++) puts(art[i]);
  putchar('\n');
  return 1;
}


/* ---- pictures ---- */

/* A small canvas drawn the way chafa draws an image into a terminal: two
   pixels to a cell with the upper half block, the top pixel as the
   foreground colour and the bottom one as the background, in the
   256-colour cube. Animations redraw in place a few times a second and
   leave the last frame; without a terminal there is one frame. */

enum { CW = 72, CH = 48 };                 /* pixels: 72 cells wide, 24 rows high */
typedef struct { unsigned char r, g, b; } Rgb;
static Rgb canvas[CH][CW];

static int ansi256(Rgb c) {
  return 16 + 36 * (c.r * 5 / 255) + 6 * (c.g * 5 / 255) + (c.b * 5 / 255);
}

static void show_canvas(int again) {
  if (again) printf("\033[%dA", CH / 2);       /* back to the top of the previous frame */
  for (int y = 0; y < CH; y += 2) {
    fputs("   ", stdout);
    int lf = -1, lb = -1;
    for (int x = 0; x < CW; x++) {
      int f = ansi256(canvas[y][x]), b = ansi256(canvas[y + 1][x]);
      if (f != lf) { printf("\033[38;5;%dm", f); lf = f; }
      if (b != lb) { printf("\033[48;5;%dm", b); lb = b; }
      fputs("▀", stdout);
    }
    fputs("\033[0m\n", stdout);
  }
  fflush(stdout);
}

static void pause_ms(int ms) {
  struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
  nanosleep(&t, NULL);
}

static Rgb lerp(Rgb a, Rgb b, double t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  Rgb c = { (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t) };
  return c;
}

/* t in [0,1] through a palette of n stops */
static Rgb palette(const Rgb *stops, int n, double t) {
  if (t <= 0) return stops[0];
  if (t >= 1) return stops[n - 1];
  double u = t * (n - 1);
  int i = (int)u;
  return lerp(stops[i], stops[i + 1], u - i);
}

/* the Mandelbrot set, or a Julia set for c */
static int fractal(int julia, double cr, double ci) {
  static const Rgb stops[] = { { 0, 0, 40 }, { 0, 40, 160 }, { 0, 200, 220 }, { 240, 240, 120 }, { 255, 120, 0 }, { 60, 0, 0 } };
  double x0 = julia ? -1.8 : -2.3, x1 = julia ? 1.8 : 0.9, y0 = -1.2, y1 = 1.2;
  const int maxit = 96;
  for (int py = 0; py < CH; py++)
    for (int px = 0; px < CW; px++) {
      double a = x0 + (x1 - x0) * (px + 0.5) / CW, b = y0 + (y1 - y0) * (py + 0.5) / CH;
      double zr = julia ? a : 0, zi = julia ? b : 0, kr = julia ? cr : a, ki = julia ? ci : b;
      int it = 0;
      while (it < maxit && zr * zr + zi * zi < 64) { double t = zr * zr - zi * zi + kr; zi = 2 * zr * zi + ki; zr = t; it++; }
      if (it == maxit) { Rgb k = { 0, 0, 0 }; canvas[py][px] = k; continue; }
      double nu = it + 1 - log(log(sqrt(zr * zr + zi * zi))) / log(2.0);   /* smooth colouring */
      canvas[py][px] = palette(stops, 6, fmod(nu / 24.0, 1.0));
    }
  putchar('\n'); show_canvas(0); putchar('\n');
  return 1;
}

/* the demo-scene fire: heat rises, cools and flickers */
static int fire(void) {
  static const Rgb stops[] = { { 0, 0, 0 }, { 120, 0, 0 }, { 255, 60, 0 }, { 255, 200, 0 }, { 255, 255, 220 } };
  static unsigned char heat[CH + 1][CW];
  memset(heat, 0, sizeof heat);
  int live = isatty(STDOUT_FILENO), frames = live ? 90 : 40;
  srand(41);
  putchar('\n');
  for (int f = 0; f < frames; f++) {
    for (int x = 0; x < CW; x++) heat[CH][x] = (unsigned char)(rand() % 2 ? 255 : 170 + rand() % 60);
    for (int y = 0; y < CH; y++)
      for (int x = 0; x < CW; x++) {
        int l = x ? x - 1 : x, r = x + 1 < CW ? x + 1 : x;
        int v = (heat[y + 1][l] + heat[y + 1][x] + heat[y + 1][r] + heat[y + 1][x]) / 4 - (rand() % 14);
        heat[y][x] = (unsigned char)(v < 0 ? 0 : v);
      }
    if (!live && f < frames - 1) continue;                 /* no terminal: only the last frame */
    for (int y = 0; y < CH; y++)
      for (int x = 0; x < CW; x++) canvas[y][x] = palette(stops, 5, heat[y][x] / 255.0);
    show_canvas(live && f > 0);
    if (live) pause_ms(50);
  }
  putchar('\n');
  return 1;
}

/* Conway's Life from a random soup, a hundred generations */
static int life(void) {
  static unsigned char a[CH][CW], b[CH][CW];
  srand((unsigned)time(NULL));
  for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) a[y][x] = rand() % 3 == 0;
  int frames = isatty(STDOUT_FILENO) ? 100 : 1;
  Rgb live = { 120, 255, 120 }, dead = { 0, 20, 0 }, born = { 255, 255, 255 };
  putchar('\n');
  for (int f = 0; f < frames; f++) {
    for (int y = 0; y < CH; y++)
      for (int x = 0; x < CW; x++) {
        int n = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++)
            if (dy || dx) n += a[(y + dy + CH) % CH][(x + dx + CW) % CW];
        b[y][x] = a[y][x] ? (n == 2 || n == 3) : (n == 3);
        canvas[y][x] = b[y][x] ? (a[y][x] ? live : born) : dead;
      }
    memcpy(a, b, sizeof a);
    show_canvas(f > 0);
    if (frames > 1) pause_ms(60);
  }
  putchar('\n');
  return 1;
}

/* digital rain, in the only font the terminal has */
static int rain(void) {
  enum { ROWS = CH / 2 };
  static int head[CW], len[CW];
  static const char glyphs[] = "0123456789+-*/^.,:;=<>()[]$%@&#";
  srand((unsigned)time(NULL));
  for (int x = 0; x < CW; x++) { head[x] = -(rand() % ROWS); len[x] = 4 + rand() % 10; }
  int live = isatty(STDOUT_FILENO), frames = live ? 80 : 30;
  putchar('\n');
  for (int f = 0; f < frames; f++) {
    if (!live && f < frames - 1) {                          /* no terminal: only the last frame */
      for (int x = 0; x < CW; x++) if (++head[x] - len[x] > ROWS) { head[x] = -(rand() % 6); len[x] = 4 + rand() % 10; }
      continue;
    }
    if (f && live) printf("\033[%dA", ROWS);
    for (int y = 0; y < ROWS; y++) {
      fputs("   ", stdout);
      for (int x = 0; x < CW; x++) {
        int d = head[x] - y;
        if (d < 0 || d >= len[x]) { putchar(' '); continue; }
        int g = d == 0 ? 231 : d < 3 ? 46 : d < 7 ? 40 : 22;          /* white head, fading greens */
        printf("\033[38;5;%dm%c\033[0m", g, glyphs[(x * 7 + y * 3 + f) % (int)(sizeof glyphs - 1)]);
      }
      putchar('\n');
    }
    fflush(stdout);
    for (int x = 0; x < CW; x++) if (++head[x] - len[x] > ROWS) { head[x] = -(rand() % 6); len[x] = 4 + rand() % 10; }
    if (live) pause_ms(70);
  }
  putchar('\n');
  return 1;
}

/* ---- the code ---- */

static int code_state = 0;

/* What an HP-41 said when the batteries had been out too long. The skin
   advances to the next one. */
static int memory_lost(void) {
  skin = skin == SKIN_PLAIN ? SKIN_HP41 : (skin + 1) % SKIN_COUNT;
  putchar('\n'); skin_frame41("MEMORY LOST", NULL); putchar('\n');
  return 1;
}

static int konami(char *const *toks, int ntok) {
  static const char *const code[] = { "up", "up", "down", "down", "left", "right", "left", "right", "b", "a" };
  enum { CODE_LEN = sizeof code / sizeof code[0] };
  for (int i = 0; i < ntok; i++) {
    int known = 0;
    for (size_t k = 0; k < CODE_LEN; k++) if (strcmp(toks[i], code[k]) == 0) known = 1;
    if (!known) { code_state = 0; return 0; }
  }
  for (int i = 0; i < ntok; i++) {
    if (strcmp(toks[i], code[code_state]) == 0) code_state++;
    else code_state = strcmp(toks[i], code[0]) == 0 ? 1 : 0;
    if (code_state == CODE_LEN) { code_state = 0; memory_lost(); }
  }
  return 1;
}

/* ---- the gate ---- */

int egg_line(const char *line) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s", line);
  int ntok = 0;
  char *toks[64];
  for (char *t = strtok(buf, " \t"); t && ntok < 64; t = strtok(NULL, " \t")) toks[ntok++] = t;
  if (ntok == 0) return 0;
  for (int i = 0; i < ntok; i++)
    if (find_op(toks[i]) >= 0 || find_word(toks[i])) { code_state = 0; return 0; }   /* theirs, not ours */
  if (ntok == 1) {
    const char *w = toks[0];
    if (!strcmp(w, "fuck"))  return say("Your place or mine?");
    if (!strcmp(w, "xyzzy")) return say("Nothing happens.");
    if (!strcmp(w, "plugh")) return say("A hollow voice says \"Fool.\"");
    if (!strcmp(w, "forth")) return say("ok");
    if (!strcmp(w, "hvala")) return say("Ni za kaj.");
    if (!strcmp(w, "hp41") || !strcmp(w, "hp-41") || !strcmp(w, "41c")) return hp41();
    if (!strcmp(w, "mandel") || !strcmp(w, "mandelbrot")) return fractal(0, 0, 0);
    if (!strcmp(w, "julia")) return fractal(1, -0.8, 0.156);
    if (!strcmp(w, "fire")) return fire();
    if (!strcmp(w, "life")) return life();
    if (!strcmp(w, "rain") || !strcmp(w, "matrix")) return rain();
  }
  return konami(toks, ntok);
}
