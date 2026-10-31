/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#include "decimal.h"
#include "errors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

decContext dec_ctx;
bool       decimal_mode = false;

void dec_init(void) {
  decContextDefault(&dec_ctx, DEC_INIT_BASE);
  dec_ctx.digits = DECNUMDIGITS;
  dec_ctx.round  = DEC_ROUND_HALF_EVEN;
  dec_ctx.emax   = 999999;
  dec_ctx.emin   = -999999;
  dec_ctx.traps  = 0;
}

/* Only the hard conditions are errors; Inexact and Rounded are normal. */
int dec_check(const char *what) {
  uint32_t s = dec_ctx.status;
  uint32_t hard = DEC_Division_by_zero | DEC_Invalid_operation | DEC_Overflow |
                  DEC_Division_undefined | DEC_Division_impossible;
  decContextZeroStatus(&dec_ctx);
  if (s & DEC_Division_by_zero) return mm_error("%s: division by zero", what);
  if (s & hard) return mm_error("%s: not defined for this decimal argument", what);
  return 0;
}

int dec_from_string(mm_dec *out, const char *s) {
  decContextZeroStatus(&dec_ctx);
  decNumberFromString(out, s, &dec_ctx);
  bool bad = (dec_ctx.status & DEC_Conversion_syntax) || decNumberIsNaN(out) || decNumberIsInfinite(out);
  decContextZeroStatus(&dec_ctx);
  return bad ? -1 : 0;
}

/* The shortest decimal string that reads back as exactly x: 0.1 -> "0.1". */
void dec_from_double(mm_dec *out, double x) {
  char buf[40];
  if (!isfinite(x)) { decNumberZero(out); return; }
  for (int p = 1; p <= 17; p++) {
    snprintf(buf, sizeof buf, "%.*g", p, x);
    if (strtod(buf, NULL) == x) break;
  }
  if (x == 0.0 && signbit(x)) strcpy(buf, "0");
  decContextZeroStatus(&dec_ctx);
  decNumberFromString(out, buf, &dec_ctx);
  decContextZeroStatus(&dec_ctx);
}

void dec_from_long(mm_dec *out, long v) {
  char buf[32];
  snprintf(buf, sizeof buf, "%ld", v);
  decContextZeroStatus(&dec_ctx);
  decNumberFromString(out, buf, &dec_ctx);
  decContextZeroStatus(&dec_ctx);
}

double dec_to_double(const mm_dec *a) {
  char buf[DECNUMDIGITS + 16];
  decNumberToString(a, buf);
  return strtod(buf, NULL);
}

long dec_to_long(const mm_dec *a, bool *ok) {
  mm_dec t, z;
  decNumberZero(&z);
  decContextZeroStatus(&dec_ctx);
  decNumberRescale(&t, a, &z, &dec_ctx);       /* exponent 0; inexact if it had a fraction */
  bool fine = !(dec_ctx.status & (DEC_Inexact | DEC_Invalid_operation));
  int32_t v = decNumberToInt32(&t, &dec_ctx);
  if (dec_ctx.status & DEC_Invalid_operation) fine = false;
  decContextZeroStatus(&dec_ctx);
  if (ok) *ok = fine;
  return v;
}

void dec_to_string(const mm_dec *a, char *buf, size_t n) {
  char tmp[DECNUMDIGITS + 16];
  decNumberToString(a, tmp);
  snprintf(buf, n, "%s", tmp);
}

void dec_to_fixed(const mm_dec *a, int places, char *buf, size_t n) {
  mm_dec scaled, expo;
  char tmp[DECNUMDIGITS + 48];
  dec_from_long(&expo, -(long)places);
  decContextZeroStatus(&dec_ctx);
  decNumberRescale(&scaled, a, &expo, &dec_ctx);
  if (dec_ctx.status & DEC_Invalid_operation) {       /* too large for the requested scale */
    decContextZeroStatus(&dec_ctx);
    decNumberToString(a, tmp);
  } else {
    decContextZeroStatus(&dec_ctx);
    if (decNumberIsZero(&scaled)) scaled.bits &= (uint8_t)~DECNEG;    /* -0 -> 0 */
    decNumberToString(&scaled, tmp);
  }
  snprintf(buf, n, "%s", tmp);
}

#define BIN(name, fn, what) \
  int name(mm_dec *r, const mm_dec *a, const mm_dec *b) { \
    decContextZeroStatus(&dec_ctx); fn(r, a, b, &dec_ctx); return dec_check(what); }
#define UN(name, fn, what) \
  int name(mm_dec *r, const mm_dec *a) { \
    decContextZeroStatus(&dec_ctx); fn(r, a, &dec_ctx); return dec_check(what); }

BIN(dec_add, decNumberAdd, "+")
BIN(dec_sub, decNumberSubtract, "-")
BIN(dec_mul, decNumberMultiply, "*")
BIN(dec_div, decNumberDivide, "/")
BIN(dec_pow, decNumberPower, "^")
UN(dec_neg, decNumberMinus, "chs")
UN(dec_abs, decNumberAbs, "abs")
UN(dec_sqrt, decNumberSquareRoot, "sqrt")
UN(dec_exp, decNumberExp, "exp")
UN(dec_ln, decNumberLn, "ln")
UN(dec_log10, decNumberLog10, "log")

int dec_intg(mm_dec *r, const mm_dec *a) {
  decContext c = dec_ctx;
  c.round = DEC_ROUND_DOWN;
  decContextZeroStatus(&c);
  decNumberToIntegralValue(r, a, &c);
  return 0;
}

int dec_round(mm_dec *r, const mm_dec *a, int places) {
  mm_dec expo;
  dec_from_long(&expo, -(long)places);
  decContextZeroStatus(&dec_ctx);
  decNumberRescale(r, a, &expo, &dec_ctx);
  if (dec_ctx.status & DEC_Invalid_operation) {
    decContextZeroStatus(&dec_ctx);
    return mm_error("round: cannot round this number to %d places", places);
  }
  decContextZeroStatus(&dec_ctx);
  return 0;
}

int dec_compare(const mm_dec *a, const mm_dec *b) {
  mm_dec t;
  decContextZeroStatus(&dec_ctx);
  decNumberCompare(&t, a, b, &dec_ctx);
  decContextZeroStatus(&dec_ctx);
  if (decNumberIsZero(&t)) return 0;
  return decNumberIsNegative(&t) ? -1 : 1;
}

bool dec_is_zero(const mm_dec *a) { return decNumberIsZero(a); }
bool dec_is_neg(const mm_dec *a)  { return decNumberIsNegative(a) && !decNumberIsZero(a); }
