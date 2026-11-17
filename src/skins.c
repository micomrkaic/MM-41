/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Display skins. Three classic displays, drawn in the terminal:
 *
 *   hp41   one twelve-character LCD line per level, the top level framed
 *          with the 41C's annunciators; numbers in the 41's FIX/SCI
 *          format (ten-digit mantissa, comma separators, the exponent
 *          after a space or a minus, as in 1.234568-03)
 *   hp12c  a seven-segment display in block elements, two rows per
 *          level, the same ten-digit arithmetic and the BEGIN annunciator
 *   hp48   the 48's screen at text size, and hp48px the same as a dot
 *          matrix: a 5 x 7 font rendered in quadrant blocks,
 *          the { HOME } line, levels numbered from the top as the 48 did,
 *          values right-justified, 22 characters a line
 *
 * Every level of the stack is drawn, deepest first, the top of the stack
 * last. The skin changes only this listing; pm, help, the tables and the
 * error messages are the same in every skin.
 */

#include "skins.h"
#include "decimal.h"
#include "errors.h"
#include "globals.h"
#include "ops.h"
#include "stack.h"
#include "words.h"
#include <ctype.h>
#include <gsl/gsl_complex.h>
#include <gsl/gsl_complex_math.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int skin = SKIN_PLAIN;
int ann_begin = 0;

static const char *const names[SKIN_COUNT] = { "plain", "hp41", "hp12c", "hp48", "hp48px" };

const char *skin_name(int s) { return s >= 0 && s < SKIN_COUNT ? names[s] : "plain"; }

int skin_from_name(const char *name) {
  for (int i = 0; i < SKIN_COUNT; i++) if (strcmp(name, names[i]) == 0) return i;
  return -1;
}

/* ------------------------------------------------------------------ */
/* The number format of the HP-41 and the HP-12C: a ten-digit mantissa.
   FIX p shows p decimals, fewer when the integer part needs the digits,
   with comma separators and a trailing point when no decimals fit; a
   number that does not fit, or would show as zero, switches to SCI with
   up to six decimals and a two-digit exponent: 1.234568-03, 6.022137 23. */

static void group_digits(const char *digits, char *out) {
  size_t n = strlen(digits), o = 0;
  for (size_t i = 0; i < n; i++) {
    out[o++] = digits[i];
    size_t left = n - 1 - i;
    if (left > 0 && left % 3 == 0) out[o++] = ',';
  }
  out[o] = '\0';
}

static void lcd_number(double x, int prec, char *out, size_t cap) {
  if (isnan(x)) { snprintf(out, cap, "NAN"); return; }
  if (isinf(x)) { snprintf(out, cap, "%s9.999999 99", x < 0 ? "-" : ""); return; }
  double ax = fabs(x);
  const char *sign = x < 0 ? "-" : "";
  if (prec < 0) prec = 0;
  if (prec > 9) prec = 9;
  char buf[64];

  if (ax < 1e10) {
    int nint = ax < 1 ? 1 : (int)floor(log10(ax)) + 1;
    int dec = prec;
    if (nint + dec > 10) dec = 10 - nint;
    for (int pass = 0; pass < 2; pass++) {
      snprintf(buf, sizeof buf, "%.*f", dec, ax);
      char *dot = strchr(buf, '.');
      size_t ni = dot ? (size_t)(dot - buf) : strlen(buf);
      if (ni > 10) break;                                      /* rounded up past ten digits: SCI */
      if ((int)ni + dec > 10) { dec = 10 - (int)ni; if (dec < 0) dec = 0; continue; }   /* 999.99 -> 1000.0: one decimal fewer */
      int nonzero = 0;
      for (char *p = buf; *p; p++) if (*p >= '1' && *p <= '9') nonzero = 1;
      if (ax != 0 && !nonzero) break;                          /* would show as zero: SCI */
      char ip[16], grouped[24];
      snprintf(ip, sizeof ip, "%.*s", (int)ni, buf);
      group_digits(ip, grouped);
      snprintf(out, cap, "%s%s.%s", sign, grouped, dot ? dot + 1 : "");
      return;
    }
  }

  int d = prec > 6 ? 6 : prec;
  for (;;) {
    snprintf(buf, sizeof buf, "%.*e", d, ax);                /* 1.234568e-03 */
    char *e = strchr(buf, 'e');
    int ex = atoi(e + 1);
    *e = '\0';
    if ((ex >= 100 || ex <= -100) && d > 0) { d--; continue; } /* three-digit exponent takes a mantissa digit */
    if (ex >= 100 && d == 0) { snprintf(out, cap, "%s9.999999 99", sign); return; }
    snprintf(out, cap, "%s%s%c%02d", sign, buf, ex < 0 ? '-' : ' ', abs(ex));
    return;
  }
}

/* ------------------------------------------------------------------ */
/* What a level shows as, in the 41's and the 12C's twelve-character
   terms. Strings go to ALPHA in capitals, matrices to a size, complex
   numbers to re+Jim (the 41Z's convention). */

static void level_text_41(const stack_element *e, char *out, size_t cap) {
  switch (e->type) {
  case TYPE_REAL:    lcd_number(e->real, print_precision, out, cap); break;
  case TYPE_DECIMAL: lcd_number(dec_to_double(&e->dec), print_precision, out, cap); break;
  case TYPE_STRING: {
    size_t n = 0;
    for (const char *p = e->string; *p && n + 1 < cap && n < 12; p++) out[n++] = (char)toupper((unsigned char)*p);
    out[n] = '\0';
    break;
  }
  case TYPE_COMPLEX: {
    double re = GSL_REAL(e->complex_val), im = GSL_IMAG(e->complex_val);
    snprintf(out, cap, "%.4g%cJ%.4g", re, im < 0 ? '-' : '+', fabs(im));
    break;
  }
  case TYPE_MATRIX_REAL:    snprintf(out, cap, "MAT %zuX%zu", e->matrix_real->size1, e->matrix_real->size2); break;
  case TYPE_MATRIX_COMPLEX: snprintf(out, cap, "CMAT %zuX%zu", e->matrix_complex->size1, e->matrix_complex->size2); break;
  default: out[0] = '\0';
  }
}

/* X Y Z T for the four nearest the top, then register numbers */
static void level_label(int depth, char *out, size_t cap) {
  static const char xyzt[] = "XYZT";
  if (depth < 4) snprintf(out, cap, "%c", xyzt[depth]);
  else snprintf(out, cap, "%d", depth);
}

/* ---- hp41 ---- */

#define LCD41 16   /* twelve positions plus room for the commas and the point */

static const char bar[] = "────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────";

/* the framed X register with its annunciator row (NULL: none) */
static void frame41(const char *text, const char *ann) {
  printf("      ┌%.*s┐\n", (LCD41 + 2) * 3, bar);
  printf("%4s  │ %-*s │\n", "X", LCD41, text);
  if (ann) printf("      │ %-*s │\n", LCD41, ann);
  printf("      └%.*s┘\n", (LCD41 + 2) * 3, bar);
}

static void print_hp41(const Stack *s) {
  char text[64], label[8], ann[LCD41 + 1];
  for (int i = 0; i < s->top; i++) {               /* the deeper levels, one line each */
    level_label(s->top - i, label, sizeof label);
    level_text_41(&s->items[i], text, sizeof text);
    printf("%4s    %s\n", label, text);
  }
  const stack_element *x = &s->items[s->top];
  level_text_41(x, text, sizeof text);
  /* annunciators: BAT USER G RAD SHIFT 0 1 2 3 4 PRGM ALPHA -- the ones that mean something here */
  snprintf(ann, sizeof ann, "%s%s%s%*s", word_count > 0 ? "USER " : "     ", "RAD ", decimal_mode ? "0 " : "  ",
           LCD41 - 11, x->type == TYPE_STRING ? "ALPHA" : "");
  frame41(text, ann);
}

/* ---- hp12c ---- */

/* segments a b c d e f g as bits 0..6 */
static unsigned char seg7(char c) {
  switch (c) {
  case '0': return 0x3f; case '1': return 0x06; case '2': return 0x5b; case '3': return 0x4f;
  case '4': return 0x66; case '5': return 0x6d; case '6': return 0x7d; case '7': return 0x07;
  case '8': return 0x7f; case '9': return 0x6f; case '-': return 0x40;
  case 'E': return 0x79; case 'r': return 0x50; case 'o': return 0x5c; case 'n': return 0x54;
  case 'u': return 0x1c; case 'i': return 0x04; case 'g': return 0x6f; case 'A': return 0x77;
  case 'N': return 0x37;
  default:  return 0x00;
  }
}

/* Draw `text` as 11 positions (sign and ten digits) of seven-segment
   glyphs in block elements, three cells wide and two high -- at the 1:2
   shape of a terminal cell that is the proportion of a real digit. The
   top bar is the upper half of the middle cell, the middle bar its lower
   half (both lit: the whole cell), the bottom bar the lower half of the
   cell below; the verticals are left and right half blocks. Punctuation
   sits in the gap after a digit. Block elements are drawn by the terminal
   itself, so the strokes are solid whatever the font. */
#define SEG_ROWS 2
static void seg_rows(const char *text, char rows[SEG_ROWS][256]) {
  char cells[SEG_ROWS][512] = { "", "" };
  int positions = 0;
  for (const char *p = text; *p; p++) {
    if (*p == '.' || *p == ',') {                   /* the gap after the previous glyph, bottom row */
      size_t l = strlen(cells[1]);
      if (l) cells[1][l - 1] = *p;
      continue;
    }
    unsigned char g = *p == ' ' ? 0 : seg7(*p);
    int a = g & 0x01, b = g & 0x02, c = g & 0x04, d = g & 0x08, e = g & 0x10, f = g & 0x20, m = g & 0x40;
    snprintf(cells[0] + strlen(cells[0]), 64, "%s%s%s ", f ? "▌" : " ", a && m ? "█" : a ? "▀" : m ? "▄" : " ", b ? "▐" : " ");
    snprintf(cells[1] + strlen(cells[1]), 64, "%s%s%s ", e ? "▌" : " ", d ? "▄" : " ", c ? "▐" : " ");
    positions++;
  }
  /* right-justify in 11 positions: the 12C lights digits from the right */
  int pad = 11 - positions;
  if (pad < 0) pad = 0;
  for (int r = 0; r < SEG_ROWS; r++) {
    memset(rows[r], ' ', (size_t)pad * 4);
    rows[r][pad * 4] = '\0';
    strncat(rows[r], cells[r], 255 - (size_t)pad * 4);
  }
}

static void level_text_12c(const stack_element *e, char *out, size_t cap) {
  switch (e->type) {
  case TYPE_REAL:    lcd_number(e->real, print_precision, out, cap); break;
  case TYPE_DECIMAL: lcd_number(dec_to_double(&e->dec), print_precision, out, cap); break;
  default:           snprintf(out, cap, "----------"); break;    /* the 12C has no such thing */
  }
}

#define LCD12 44

static void frame12c(const char *text) {
  char rows[SEG_ROWS][256];
  seg_rows(text, rows);
  printf("      ┌─%.*s─┐\n", LCD12 * 3, bar);
  printf("%4s  │ %s │\n", "X", rows[0]);
  printf("      │ %s │\n", rows[1]);
  printf("      │ %-*s │\n", LCD12, ann_begin ? "BEGIN" : "");
  printf("      └─%.*s─┘\n", LCD12 * 3, bar);
}

static void print_hp12c(const Stack *s) {
  char text[64], label[8], rows[SEG_ROWS][256];
  for (int i = 0; i < s->top; i++) {
    level_label(s->top - i, label, sizeof label);
    level_text_12c(&s->items[i], text, sizeof text);
    seg_rows(text, rows);
    printf("%4s    %s\n        %s\n\n", label, rows[0], rows[1]);
  }
  level_text_12c(&s->items[s->top], text, sizeof text);
  frame12c(text);
}

/* ---- hp48 ---- */

/* 5 x 7 font: each glyph seven rows of five bits, top row first */
typedef struct { char c; unsigned char rows[7]; } Glyph;
static const Glyph font[] = {
  { ' ', { 0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
  { '0', { 0x0e,0x11,0x13,0x15,0x19,0x11,0x0e } },
  { '1', { 0x04,0x0c,0x04,0x04,0x04,0x04,0x0e } },
  { '2', { 0x0e,0x11,0x01,0x02,0x04,0x08,0x1f } },
  { '3', { 0x1f,0x02,0x04,0x02,0x01,0x11,0x0e } },
  { '4', { 0x02,0x06,0x0a,0x12,0x1f,0x02,0x02 } },
  { '5', { 0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e } },
  { '6', { 0x06,0x08,0x10,0x1e,0x11,0x11,0x0e } },
  { '7', { 0x1f,0x01,0x02,0x04,0x08,0x08,0x08 } },
  { '8', { 0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e } },
  { '9', { 0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c } },
  { '.', { 0x00,0x00,0x00,0x00,0x00,0x0c,0x0c } },
  { ',', { 0x00,0x00,0x00,0x00,0x0c,0x04,0x08 } },
  { ':', { 0x00,0x0c,0x0c,0x00,0x0c,0x0c,0x00 } },
  { ';', { 0x00,0x0c,0x0c,0x00,0x0c,0x04,0x08 } },
  { '-', { 0x00,0x00,0x00,0x1f,0x00,0x00,0x00 } },
  { '+', { 0x00,0x04,0x04,0x1f,0x04,0x04,0x00 } },
  { '*', { 0x00,0x04,0x15,0x0e,0x15,0x04,0x00 } },
  { '/', { 0x00,0x01,0x02,0x04,0x08,0x10,0x00 } },
  { '\\',{ 0x00,0x10,0x08,0x04,0x02,0x01,0x00 } },
  { '=', { 0x00,0x00,0x1f,0x00,0x1f,0x00,0x00 } },
  { '^', { 0x04,0x0a,0x11,0x00,0x00,0x00,0x00 } },
  { '(', { 0x02,0x04,0x08,0x08,0x08,0x04,0x02 } },
  { ')', { 0x08,0x04,0x02,0x02,0x02,0x04,0x08 } },
  { '[', { 0x0e,0x08,0x08,0x08,0x08,0x08,0x0e } },
  { ']', { 0x0e,0x02,0x02,0x02,0x02,0x02,0x0e } },
  { '{', { 0x02,0x04,0x04,0x08,0x04,0x04,0x02 } },
  { '}', { 0x08,0x04,0x04,0x02,0x04,0x04,0x08 } },
  { '"', { 0x0a,0x0a,0x0a,0x00,0x00,0x00,0x00 } },
  { '\'',{ 0x04,0x04,0x04,0x00,0x00,0x00,0x00 } },
  { '!', { 0x04,0x04,0x04,0x04,0x00,0x00,0x04 } },
  { '?', { 0x0e,0x11,0x01,0x02,0x04,0x00,0x04 } },
  { '#', { 0x0a,0x0a,0x1f,0x0a,0x1f,0x0a,0x0a } },
  { '%', { 0x18,0x19,0x02,0x04,0x08,0x13,0x03 } },
  { '$', { 0x04,0x0f,0x14,0x0e,0x05,0x1e,0x04 } },
  { '<', { 0x02,0x04,0x08,0x10,0x08,0x04,0x02 } },
  { '>', { 0x08,0x04,0x02,0x01,0x02,0x04,0x08 } },
  { '_', { 0x00,0x00,0x00,0x00,0x00,0x00,0x1f } },
  { '@', { 0x0e,0x11,0x01,0x0d,0x15,0x15,0x0e } },
  { '&', { 0x0c,0x12,0x14,0x08,0x15,0x12,0x0d } },
  { '|', { 0x04,0x04,0x04,0x04,0x04,0x04,0x04 } },
  { 'A', { 0x0e,0x11,0x11,0x1f,0x11,0x11,0x11 } },
  { 'B', { 0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e } },
  { 'C', { 0x0e,0x11,0x10,0x10,0x10,0x11,0x0e } },
  { 'D', { 0x1c,0x12,0x11,0x11,0x11,0x12,0x1c } },
  { 'E', { 0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f } },
  { 'F', { 0x1f,0x10,0x10,0x1e,0x10,0x10,0x10 } },
  { 'G', { 0x0e,0x11,0x10,0x17,0x11,0x11,0x0f } },
  { 'H', { 0x11,0x11,0x11,0x1f,0x11,0x11,0x11 } },
  { 'I', { 0x0e,0x04,0x04,0x04,0x04,0x04,0x0e } },
  { 'J', { 0x07,0x02,0x02,0x02,0x02,0x12,0x0c } },
  { 'K', { 0x11,0x12,0x14,0x18,0x14,0x12,0x11 } },
  { 'L', { 0x10,0x10,0x10,0x10,0x10,0x10,0x1f } },
  { 'M', { 0x11,0x1b,0x15,0x15,0x11,0x11,0x11 } },
  { 'N', { 0x11,0x11,0x19,0x15,0x13,0x11,0x11 } },
  { 'O', { 0x0e,0x11,0x11,0x11,0x11,0x11,0x0e } },
  { 'P', { 0x1e,0x11,0x11,0x1e,0x10,0x10,0x10 } },
  { 'Q', { 0x0e,0x11,0x11,0x11,0x15,0x12,0x0d } },
  { 'R', { 0x1e,0x11,0x11,0x1e,0x14,0x12,0x11 } },
  { 'S', { 0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e } },
  { 'T', { 0x1f,0x04,0x04,0x04,0x04,0x04,0x04 } },
  { 'U', { 0x11,0x11,0x11,0x11,0x11,0x11,0x0e } },
  { 'V', { 0x11,0x11,0x11,0x11,0x11,0x0a,0x04 } },
  { 'W', { 0x11,0x11,0x11,0x15,0x15,0x15,0x0a } },
  { 'X', { 0x11,0x11,0x0a,0x04,0x0a,0x11,0x11 } },
  { 'Y', { 0x11,0x11,0x11,0x0a,0x04,0x04,0x04 } },
  { 'Z', { 0x1f,0x01,0x02,0x04,0x08,0x10,0x1f } },
  { 'a', { 0x00,0x00,0x0e,0x01,0x0f,0x11,0x0f } },
  { 'b', { 0x10,0x10,0x16,0x19,0x11,0x11,0x1e } },
  { 'c', { 0x00,0x00,0x0e,0x10,0x10,0x11,0x0e } },
  { 'd', { 0x01,0x01,0x0d,0x13,0x11,0x11,0x0f } },
  { 'e', { 0x00,0x00,0x0e,0x11,0x1f,0x10,0x0e } },
  { 'f', { 0x06,0x09,0x08,0x1c,0x08,0x08,0x08 } },
  { 'g', { 0x00,0x0f,0x11,0x11,0x0f,0x01,0x0e } },
  { 'h', { 0x10,0x10,0x16,0x19,0x11,0x11,0x11 } },
  { 'i', { 0x04,0x00,0x0c,0x04,0x04,0x04,0x0e } },
  { 'j', { 0x02,0x00,0x06,0x02,0x02,0x12,0x0c } },
  { 'k', { 0x10,0x10,0x12,0x14,0x18,0x14,0x12 } },
  { 'l', { 0x0c,0x04,0x04,0x04,0x04,0x04,0x0e } },
  { 'm', { 0x00,0x00,0x1a,0x15,0x15,0x11,0x11 } },
  { 'n', { 0x00,0x00,0x16,0x19,0x11,0x11,0x11 } },
  { 'o', { 0x00,0x00,0x0e,0x11,0x11,0x11,0x0e } },
  { 'p', { 0x00,0x00,0x1e,0x11,0x1e,0x10,0x10 } },
  { 'q', { 0x00,0x00,0x0d,0x13,0x0f,0x01,0x01 } },
  { 'r', { 0x00,0x00,0x16,0x19,0x10,0x10,0x10 } },
  { 's', { 0x00,0x00,0x0e,0x10,0x0e,0x01,0x1e } },
  { 't', { 0x08,0x08,0x1c,0x08,0x08,0x09,0x06 } },
  { 'u', { 0x00,0x00,0x11,0x11,0x11,0x13,0x0d } },
  { 'v', { 0x00,0x00,0x11,0x11,0x11,0x0a,0x04 } },
  { 'w', { 0x00,0x00,0x11,0x11,0x15,0x15,0x0a } },
  { 'x', { 0x00,0x00,0x11,0x0a,0x04,0x0a,0x11 } },
  { 'y', { 0x00,0x00,0x11,0x11,0x0f,0x01,0x0e } },
  { 'z', { 0x00,0x00,0x1f,0x02,0x04,0x08,0x1f } },
};

static const unsigned char *glyph(char c) {
  for (size_t i = 0; i < sizeof font / sizeof font[0]; i++) if (font[i].c == c) return font[i].rows;
  return font[0].rows;                                     /* unknown: blank */
}

#define HP48_COLS 22                       /* 131 pixels / 6 per character */
#define HP48_PX   (HP48_COLS * 6)          /* 132, even, so the cells pair up */

/* Draw one 22-character text line as four rows of quadrant blocks, each
   cell two pixels by two. The font occupies 7 rows; the eighth is the line
   gap. Quadrant blocks are drawn by the terminal itself, so the pixels are
   solid and contiguous in any font. */
static void pixel_line(const char *text) {
  unsigned char px[8][HP48_PX];
  memset(px, 0, sizeof px);
  for (int c = 0; c < HP48_COLS && text[c]; c++) {
    const unsigned char *g = glyph(text[c]);
    for (int r = 0; r < 7; r++)
      for (int b = 0; b < 5; b++)
        if (g[r] & (0x10 >> b)) px[r][c * 6 + b] = 1;
  }
  /* quadrants indexed by top-left, top-right, bottom-left, bottom-right */
  static const char *const quad[16] = { " ", "▘", "▝", "▀", "▖", "▌", "▞", "▛", "▗", "▚", "▐", "▜", "▄", "▙", "▟", "█" };
  for (int r = 0; r < 8; r += 2) {
    char line[HP48_PX / 2 * 4 + 1]; size_t o = 0;
    for (int x = 0; x < HP48_PX; x += 2) {
      int v = (px[r][x] ? 1 : 0) | (px[r][x + 1] ? 2 : 0) | (px[r + 1][x] ? 4 : 0) | (px[r + 1][x + 1] ? 8 : 0);
      size_t l = strlen(quad[v]);
      memcpy(line + o, quad[v], l); o += l;
    }
    line[o] = '\0';
    printf("   │%s│\n", line);
  }
}

/* The 48's number display: STD shows up to twelve significant digits with
   no trailing zeros and no leading zero (.5, 1.E-12); FIX p shows p
   decimals. Either falls back to exponent form when twelve digits run out. */
static void hp48_number(double x, char *out, size_t cap) {
  if (isnan(x)) { snprintf(out, cap, "NaN"); return; }
  if (isinf(x)) { snprintf(out, cap, "%sMAXR", x < 0 ? "-" : ""); return; }
  double ax = fabs(x);
  char buf[64];
  if (fixed_point && (ax == 0 || (ax < 1e12 && ax >= pow(10, -print_precision) / 2))) {
    snprintf(buf, sizeof buf, "%.*f", print_precision, x);
  } else if (ax == 0 || (ax >= 1e-4 && ax < 1e12)) {
    snprintf(buf, sizeof buf, "%.12g", x);
    if (strchr(buf, 'e')) snprintf(buf, sizeof buf, "%.11e", x);  /* %g chose exponent form: redo below */
  } else {
    snprintf(buf, sizeof buf, "%.11e", x);
  }
  char *e = strchr(buf, 'e');
  if (e) {                                                   /* mantissa: trim zeros, keep the point; exponent: no padding */
    int ex = atoi(e + 1);
    *e = '\0';
    char *end = buf + strlen(buf) - 1;
    while (end > buf && *end == '0') *end-- = '\0';
    snprintf(out, cap, "%sE%d", buf, ex);
  } else {
    snprintf(out, cap, "%s", buf);
  }
  /* no leading zero before the point: 0.5 -> .5, -0.5 -> -.5 */
  char *p = out[0] == '-' ? out + 1 : out;
  if (p[0] == '0' && p[1] == '.') memmove(p, p + 1, strlen(p));
}

static void hp48_matrix_text(const gsl_matrix *m, char *out, size_t cap) {
  size_t o = 0;
  o += (size_t)snprintf(out + o, cap - o, "[");
  for (size_t i = 0; i < m->size1 && o + 4 < cap; i++) {
    o += (size_t)snprintf(out + o, cap - o, "[");
    for (size_t j = 0; j < m->size2 && o + 4 < cap; j++) {
      char n[32]; hp48_number(gsl_matrix_get(m, i, j), n, sizeof n);
      o += (size_t)snprintf(out + o, cap - o, " %s", n);
    }
    o += (size_t)snprintf(out + o, cap - o, " ]");
  }
  if (o + 1 < cap) snprintf(out + o, cap - o, "]");
}

static void level_text_48(const stack_element *e, char *out, size_t cap) {
  switch (e->type) {
  case TYPE_REAL:    hp48_number(e->real, out, cap); break;
  case TYPE_DECIMAL: hp48_number(dec_to_double(&e->dec), out, cap); break;
  case TYPE_STRING:  snprintf(out, cap, "\"%s\"", e->string); break;
  case TYPE_COMPLEX: {
    char re[32], im[32];
    hp48_number(GSL_REAL(e->complex_val), re, sizeof re); hp48_number(GSL_IMAG(e->complex_val), im, sizeof im);
    snprintf(out, cap, "(%s,%s)", re, im);
    break;
  }
  case TYPE_MATRIX_REAL:    hp48_matrix_text(e->matrix_real, out, cap); break;
  case TYPE_MATRIX_COMPLEX: snprintf(out, cap, "[[ complex %zux%zu ]]", e->matrix_complex->size1, e->matrix_complex->size2); break;
  default: out[0] = '\0';
  }
}

/* The 48's screen, at text size (pixels = 0) or as a dot matrix (1). */
static void print_hp48(const Stack *s, int pixels) {   /* s NULL: an empty stack */
  char line[64], text[256];
  int inner = pixels ? HP48_PX / 2 : HP48_COLS + 2;
  printf("   ┌%.*s┐\n", inner * 3, bar);
  snprintf(line, sizeof line, "%-*s", HP48_COLS, "RAD       { HOME }");
  if (pixels) pixel_line(line); else printf("   │ %s │\n", line);
  for (int i = 0; s && i <= s->top; i++) {
    int level = s->top - i + 1;                              /* the 48 counts 1 from the top */
    level_text_48(&s->items[i], text, sizeof text);
    char label[16]; snprintf(label, sizeof label, "%d:", level);
    int room = HP48_COLS - (int)strlen(label);
    if (room < 1) room = 1;
    if ((int)strlen(text) > room) text[room] = '\0';         /* clipped on the right, as the 48 did */
    if (room > HP48_COLS) room = HP48_COLS;
    snprintf(line, sizeof line, "%s%*.*s", label, room, room, text);
    if (pixels) pixel_line(line); else printf("   │ %s │\n", line);
  }
  printf("   └%.*s┘\n", inner * 3, bar);
}

/* ------------------------------------------------------------------ */

void skin_print_stack(const Stack *s) {
  if (s->top < 0) {                                /* an empty stack: the display shows zero, as a real one would */
    char zero[32]; lcd_number(0.0, print_precision, zero, sizeof zero);
    switch (skin) {
    case SKIN_HP41:  frame41(zero, NULL); return;
    case SKIN_HP12C: frame12c(zero); return;
    case SKIN_HP48:  print_hp48(NULL, 0); return;
    case SKIN_HP48PX: print_hp48(NULL, 1); return;
    }
    return;
  }
  switch (skin) {
  case SKIN_HP41:  print_hp41(s); break;
  case SKIN_HP12C: print_hp12c(s); break;
  case SKIN_HP48:  print_hp48(s, 0); break;
  case SKIN_HP48PX: print_hp48(s, 1); break;
  default: break;
  }
}

/* "name" skin */
int op_skin(Stack *s) {
  stack_element *e = &s->items[s->top];
  if (e->type != TYPE_STRING) return mm_error("skin: needs a name: \"plain\", \"hp41\", \"hp12c\", \"hp48\" or \"hp48px\"");
  int k = skin_from_name(e->string);
  if (k < 0) return mm_error("skin: no skin '%s'; the skins are plain, hp41, hp12c, hp48 and hp48px", e->string);
  stack_element t = pop(s); stack_element_free(&t);
  skin = k;
  return 0;
}

/* ------------------------------------------------------------------ */
/* The hidden part. A certain sequence of ten words, typed one per line or
   all on one, each of which is otherwise nothing in MM-41 (a word the user
   has defined with that name is left alone). When it completes, the
   display does what an HP-41 did when the batteries had been out too
   long, and the skin advances to the next one. */

static int secret_state = 0;

static int memory_lost(void) {
  skin = skin == SKIN_PLAIN ? SKIN_HP41 : (skin + 1) % SKIN_COUNT;
  putchar('\n'); frame41("MEMORY LOST", NULL); putchar('\n');
  return 1;
}

int skin_secret_line(const char *line) {
  static const char *const code[] = { "up", "up", "down", "down", "left", "right", "left", "right", "b", "a" };
  enum { CODE_LEN = sizeof code / sizeof code[0] };
  char buf[256];
  snprintf(buf, sizeof buf, "%s", line);
  int ntok = 0;
  char *toks[64];
  for (char *t = strtok(buf, " \t"); t && ntok < 64; t = strtok(NULL, " \t")) toks[ntok++] = t;
  if (ntok == 0) return 0;
  for (int i = 0; i < ntok; i++) {
    int known = 0;
    for (size_t k = 0; k < CODE_LEN; k++) if (strcmp(toks[i], code[k]) == 0) known = 1;
    if (!known || find_op(toks[i]) >= 0 || find_word(toks[i])) { secret_state = 0; return 0; }
  }
  int fired = 0;
  for (int i = 0; i < ntok; i++) {
    if (strcmp(toks[i], code[secret_state]) == 0) secret_state++;
    else secret_state = strcmp(toks[i], code[0]) == 0 ? 1 : 0;
    if (secret_state == CODE_LEN) { secret_state = 0; fired = memory_lost(); }
  }
  (void)fired;
  return 1;
}
