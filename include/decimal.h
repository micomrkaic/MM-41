/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The decimal number type.
 *
 * Money is decimal, and binary doubles cannot hold 0.10. A decimal is an IBM
 * decNumber (the reference implementation of the General Decimal Arithmetic
 * Specification, vendored under vendor/decNumber) with 34 significant digits
 * and round-half-even, the same arithmetic as SQL NUMERIC, Java BigDecimal and
 * Python's decimal module. It is a fixed-size struct, so a decimal on the stack
 * owns no heap memory.
 *
 * The promotion rule is "decimal absorbs binary": a decimal combined with a
 * real gives a decimal (the real is converted by its shortest round-trip
 * representation, so 0.1 becomes 0.1). Complex numbers, matrices and the
 * numerical methods are binary only; >bin converts explicitly, because that
 * is the one direction in which digits are lost. */

#ifndef MM41_DECIMAL_H
#define MM41_DECIMAL_H

#define DECNUMDIGITS 34              /* must precede decNumber.h */
#define HAVE_STDINT_H 1              /* use the system <stdint.h>, not decNumber's own typedefs */
#include <stdint.h>
#include "decNumber.h"
#include "decContext.h"
#include <stdbool.h>

typedef decNumber mm_dec;

extern decContext dec_ctx;           /* 34 digits, half-even, traps off */
extern bool       decimal_mode;      /* literals parse as decimals ("decimal"/"binary") */

void dec_init(void);                 /* set up the context; called at startup */

/* conversions */
int  dec_from_string(mm_dec *out, const char *s);       /* 0 ok, -1 not a finite decimal */
void dec_from_double(mm_dec *out, double x);            /* shortest round-trip form of x */
void dec_from_long(mm_dec *out, long v);
double dec_to_double(const mm_dec *a);
long   dec_to_long(const mm_dec *a, bool *ok);          /* exact integer value, or ok=false */
void dec_to_string(const mm_dec *a, char *buf, size_t n);            /* as stored, e.g. 0.30 */
void dec_to_fixed(const mm_dec *a, int places, char *buf, size_t n); /* rounded half-even */

/* arithmetic through the context; each reports hard errors (division by zero,
   invalid operation, overflow) with mm_error and returns -1 */
int dec_add(mm_dec *r, const mm_dec *a, const mm_dec *b);
int dec_sub(mm_dec *r, const mm_dec *a, const mm_dec *b);
int dec_mul(mm_dec *r, const mm_dec *a, const mm_dec *b);
int dec_div(mm_dec *r, const mm_dec *a, const mm_dec *b);
int dec_pow(mm_dec *r, const mm_dec *a, const mm_dec *b);
int dec_neg(mm_dec *r, const mm_dec *a);
int dec_abs(mm_dec *r, const mm_dec *a);
int dec_sqrt(mm_dec *r, const mm_dec *a);
int dec_exp(mm_dec *r, const mm_dec *a);
int dec_ln(mm_dec *r, const mm_dec *a);
int dec_log10(mm_dec *r, const mm_dec *a);
int dec_intg(mm_dec *r, const mm_dec *a);               /* toward zero */
int dec_round(mm_dec *r, const mm_dec *a, int places);  /* half-even to places decimals */
int dec_compare(const mm_dec *a, const mm_dec *b);      /* -1, 0, 1 */
bool dec_is_zero(const mm_dec *a);
bool dec_is_neg(const mm_dec *a);
int dec_check(const char *what);                        /* poll the context status */

#endif
