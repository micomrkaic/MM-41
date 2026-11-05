/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The finance module's day counts and coupon schedules, on the serial days
   of src/calendar.c (shared with the dates module). Ported from MM-B1. */

#include "fin.h"
#include "errors.h"
#include "calendar.h"

#define is_leap       cal_is_leap
#define dim           cal_days_in_month
#define days_in_year  cal_days_in_year
#define civil_to_serial cal_civil_to_serial
#define serial_to_civil cal_serial_to_civil

/* 30/360 day count; european = 1 for 30E/360, 0 for US/NASD */
static long days_30_360(long y1, int m1, int d1, long y2, int m2, int d2, int european) {
  if (european) {
    if (d1 == 31) d1 = 30;
    if (d2 == 31) d2 = 30;
  } else {
    if (d1 == 31) d1 = 30;
    if (d2 == 31 && d1 == 30) d2 = 30;
  }
  return (y2 - y1) * 360 + (m2 - m1) * 30 + (d2 - d1);
}

static long basis_days(long s1, long s2, int basis) {
  if (basis == 0 || basis == 4) {
    long y1, y2; int m1, d1, m2, d2;
    serial_to_civil(s1, &y1, &m1, &d1);
    serial_to_civil(s2, &y2, &m2, &d2);
    return days_30_360(y1, m1, d1, y2, m2, d2, basis == 4);
  }
  return s2 - s1;
}

static void ratio(mm_dec *out, long num, long den) {
  mm_dec a, b;
  dec_from_long(&a, num);
  dec_from_long(&b, den);
  decNumberDivide(out, &a, &b, &dec_ctx);
}

int fin_year_frac(long s1, long s2, int basis, mm_dec *out) {
  if (basis < 0 || basis > 4) return mm_error("yearfrac: basis must be 0..4 (30/360, act/act, act/360, act/365, 30E/360)");
  decContextZeroStatus(&dec_ctx);
  if (basis == 1) {                                   /* ISDA actual/actual */
    long y1, y2; int m1, d1, m2, d2;
    serial_to_civil(s1, &y1, &m1, &d1);
    serial_to_civil(s2, &y2, &m2, &d2);
    if (y1 == y2) { ratio(out, s2 - s1, days_in_year(y1)); return dec_check("yearfrac"); }
    mm_dec a, b, c;
    ratio(&a, civil_to_serial(y1 + 1, 1, 1) - s1, days_in_year(y1));
    ratio(&c, s2 - civil_to_serial(y2, 1, 1), days_in_year(y2));
    dec_from_long(&b, y2 - y1 - 1);
    decNumberAdd(out, &a, &b, &dec_ctx);
    decNumberAdd(out, out, &c, &dec_ctx);
    return dec_check("yearfrac");
  }
  ratio(out, basis_days(s1, s2, basis), basis == 3 ? 365 : 360);
  return dec_check("yearfrac");
}

/* Step back from a coupon date by `months`, keeping end-of-month if the
   maturity was an end-of-month date. */
static long step_back_months(long serial, int months, int eom) {
  long y; int m, d;
  serial_to_civil(serial, &y, &m, &d);
  int mm = m - months;
  while (mm < 1) { mm += 12; y -= 1; }
  int last = dim(y, mm);
  d = eom ? last : (d < last ? d : last);
  return civil_to_serial(y, mm, d);
}

/* Coupons remaining N (settle < coupon <= maturity) and the basis day counts
   A (previous coupon -> settle) and E (previous -> next coupon). */
int fin_coupon_geometry(const char *what, long settle, long maturity, int freq, int basis, int *N, long *A, long *E) {
  if (freq <= 0 || 12 % freq != 0) return mm_error("%s: coupon frequency must divide 12 (1, 2, 3, 4, 6, 12)", what);
  if (basis < 0 || basis > 4) return mm_error("%s: basis must be 0..4", what);
  if (settle >= maturity) return mm_error("%s: settlement must precede maturity", what);
  int p = 12 / freq;
  long my; int mm, md;
  serial_to_civil(maturity, &my, &mm, &md);
  int eom = (md == dim(my, mm));
  long cur = maturity, ncd = maturity;
  int n = 0;
  while (cur > settle) {
    n++;
    ncd = cur;
    cur = step_back_months(cur, p, eom);
  }
  long pcd = cur;
  *A = basis_days(pcd, settle, basis);
  *E = basis_days(pcd, ncd, basis);
  *N = n;
  return 0;
}
