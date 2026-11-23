/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The finance kernel, ported from MM-B1. Everything is computed in 34-digit
   decimals through the shared context. Inside a computation the decNumber
   calls are used directly and the context status is checked once at the end
   (dec_check), so that a scan of the rate solver does not report every
   intermediate overflow. */

#include "fin.h"
#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

mm_dec T_n, T_i, T_pv, T_pmt, T_fv, T_g;
int    T_begin = 0;

void fin_tvm_reset(void) {
  decNumberZero(&T_n); decNumberZero(&T_i); decNumberZero(&T_pv);
  decNumberZero(&T_pmt); decNumberZero(&T_fv); decNumberZero(&T_g);
  T_begin = 0;
}

/* ---- raw arithmetic (status accumulates in dec_ctx) ---- */
#define ADD(r, a, b) decNumberAdd((r), (a), (b), &dec_ctx)
#define SUB(r, a, b) decNumberSubtract((r), (a), (b), &dec_ctx)
#define MUL(r, a, b) decNumberMultiply((r), (a), (b), &dec_ctx)
#define DIV(r, a, b) decNumberDivide((r), (a), (b), &dec_ctx)
#define POW(r, a, b) decNumberPower((r), (a), (b), &dec_ctx)
#define LN(r, a)     decNumberLn((r), (a), &dec_ctx)
#define NEG(r, a)    decNumberMinus((r), (a), &dec_ctx)
#define ABS(r, a)    decNumberAbs((r), (a), &dec_ctx)
#define CPY(r, a)    decNumberCopy((r), (a))
#define INT(r, v)    dec_from_long((r), (v))
#define ISZERO(a)    decNumberIsZero(a)
#define ISNEG(a)     dec_is_neg(a)

static void onep(mm_dec *out, const mm_dec *ipct) {      /* 1 + ipct/100 */
  mm_dec h, frac, one;
  INT(&h, 100); INT(&one, 1);
  DIV(&frac, ipct, &h);
  ADD(out, &one, &frac);
}
static void frac_of(mm_dec *out, const mm_dec *ipct) {   /* ipct/100 */
  mm_dec h; INT(&h, 100); DIV(out, ipct, &h);
}
static int is_smallrate(const mm_dec *r) {               /* |r| < 1e-20 -> treat as 0 */
  mm_dec a, eps;
  ABS(&a, r);
  decNumberFromString(&eps, "1E-20", &dec_ctx);
  return dec_compare(&a, &eps) < 0;
}

/* Future-value factor of an annuity whose payment grows at g per period:
   pmt_t = pmt (1+g)^(t-1), t = 1..n, so
     A = sum (1+g)^(t-1) (1+r)^(n-t) = ((1+r)^n - (1+g)^n) / (r - g),
   with the limits n (1+r)^(n-1) when r = g and n when both are 0. With
   g = 0 it is the ordinary ((1+r)^n - 1)/r. `rn` is (1+r)^n, `o` is 1+r. */
static void ann_factor(mm_dec *A, const mm_dec *rn, const mm_dec *o, const mm_dec *rfrac, const mm_dec *gfrac, const mm_dec *n) {
  mm_dec d, one; INT(&one, 1);
  SUB(&d, rfrac, gfrac);
  if (is_smallrate(&d)) {
    if (is_smallrate(rfrac)) { CPY(A, n); return; }
    mm_dec t; DIV(&t, rn, o); MUL(A, n, &t);              /* n (1+r)^(n-1) */
    return;
  }
  if (is_smallrate(gfrac)) { mm_dec gm1; SUB(&gm1, rn, &one); DIV(A, &gm1, rfrac); return; }
  mm_dec og, gn, num;
  ADD(&og, &one, gfrac); POW(&gn, &og, n);
  SUB(&num, rn, &gn);
  DIV(A, &num, &d);
}
/* present-value annuity factor A = (1 - 1/g)/r, limit n */
static void pv_ann_factor(mm_dec *A, const mm_dec *g, const mm_dec *rfrac, const mm_dec *n) {
  if (is_smallrate(rfrac)) { CPY(A, n); return; }
  mm_dec one, inv, t; INT(&one, 1);
  DIV(&inv, &one, g);
  SUB(&t, &one, &inv);
  DIV(A, &t, rfrac);
}
/* k = 1 + r*type */
static void due_factor(mm_dec *k, const mm_dec *rfrac, int begin) {
  mm_dec one; INT(&one, 1);
  if (!begin) { CPY(k, &one); return; }
  ADD(k, &one, rfrac);
}

/* ---- TVM closed forms ---- */
int fin_tvm_fv(const mm_dec *n, const mm_dec *ipct, const mm_dec *pv, const mm_dec *pmt, const mm_dec *gpct, int begin, mm_dec *out) {
  mm_dec r, g, A, k, t1, t2, k_A, o, gr;
  decContextZeroStatus(&dec_ctx);
  frac_of(&r, ipct); frac_of(&gr, gpct); onep(&o, ipct); POW(&g, &o, n);
  ann_factor(&A, &g, &o, &r, &gr, n); due_factor(&k, &r, begin);
  MUL(&t1, pv, &g);
  MUL(&k_A, &k, &A); MUL(&t2, pmt, &k_A);
  ADD(out, &t1, &t2); NEG(out, out);
  return dec_check("?fv");
}

int fin_tvm_pv(const mm_dec *n, const mm_dec *ipct, const mm_dec *pmt, const mm_dec *fv, const mm_dec *gpct, int begin, mm_dec *out) {
  mm_dec r, g, A, k, k_A, t, o, gr;
  decContextZeroStatus(&dec_ctx);
  frac_of(&r, ipct); frac_of(&gr, gpct); onep(&o, ipct); POW(&g, &o, n);
  ann_factor(&A, &g, &o, &r, &gr, n); due_factor(&k, &r, begin);
  MUL(&k_A, &k, &A); MUL(&t, pmt, &k_A);
  ADD(&t, &t, fv); NEG(&t, &t);
  DIV(out, &t, &g);
  return dec_check("?pv");
}

int fin_tvm_pmt(const mm_dec *n, const mm_dec *ipct, const mm_dec *pv, const mm_dec *fv, const mm_dec *gpct, int begin, mm_dec *out) {
  mm_dec r, g, A, k, k_A, num, o, gr;
  decContextZeroStatus(&dec_ctx);
  frac_of(&r, ipct); frac_of(&gr, gpct); onep(&o, ipct); POW(&g, &o, n);
  ann_factor(&A, &g, &o, &r, &gr, n); due_factor(&k, &r, begin);
  MUL(&k_A, &k, &A);
  if (ISZERO(&k_A)) { decContextZeroStatus(&dec_ctx); return mm_error("?pmt: degenerate problem (n = 0)"); }
  MUL(&num, pv, &g); ADD(&num, &num, fv); NEG(&num, &num);
  DIV(out, &num, &k_A);
  return dec_check("?pmt");
}

static int solve_n_growing(const mm_dec *ipct, const mm_dec *pv, const mm_dec *pmt, const mm_dec *fv, const mm_dec *gpct, int begin, mm_dec *out);

int fin_tvm_n(const mm_dec *ipct, const mm_dec *pv, const mm_dec *pmt, const mm_dec *fv, const mm_dec *gpct, int begin, mm_dec *out) {
  mm_dec r, gr;
  decContextZeroStatus(&dec_ctx);
  frac_of(&r, ipct); frac_of(&gr, gpct);
  if (!is_smallrate(&gr)) {
    /* Divided by (1+r)^n the equation is pv + pmt k (1 - x^n)/(r-g) + fv/(1+r)^n = 0
       with x = (1+g)/(1+r). With fv = 0 that is one exponential: the level-annuity
       formula at the growth-adjusted rate. With fv != 0 there are two, so scan. */
    if (!ISZERO(fv)) return solve_n_growing(ipct, pv, pmt, fv, gpct, begin, out);
    if (ISZERO(pmt)) return mm_error("?n: pmt is zero");
    mm_dec k, d, one, o, og, x, q, t, lnt, lnx;
    INT(&one, 1);
    due_factor(&k, &r, begin); onep(&o, ipct); onep(&og, gpct);
    SUB(&d, &r, &gr);
    if (is_smallrate(&d)) {                                /* r = g: pv + pmt k n/(1+r) = 0 */
      MUL(&t, pv, &o); NEG(&t, &t); MUL(&q, pmt, &k); DIV(out, &t, &q);
      return dec_check("?n");
    }
    MUL(&q, pmt, &k); DIV(&q, &q, &d);                     /* q = pmt k/(r-g) */
    ADD(&t, pv, &q); DIV(&t, &t, &q);                      /* x^n = (pv+q)/q */
    if (ISNEG(&t) || ISZERO(&t)) { decContextZeroStatus(&dec_ctx); return mm_error("?n: no solution (the payments never repay pv at this growth)"); }
    DIV(&x, &og, &o);
    LN(&lnt, &t); LN(&lnx, &x);
    DIV(out, &lnt, &lnx);
    return dec_check("?n");
  }
  if (is_smallrate(&r)) {                                 /* n = -(pv+fv)/pmt */
    if (ISZERO(pmt)) return mm_error("?n: pmt is zero");
    mm_dec s; ADD(&s, pv, fv); NEG(&s, &s); DIV(out, &s, pmt);
    return dec_check("?n");
  }
  mm_dec k, q, o, gnum, gden, g, lng, ln1;
  due_factor(&k, &r, begin);
  MUL(&q, pmt, &k); DIV(&q, &q, &r);                      /* q = pmt*k/r */
  SUB(&gnum, &q, fv);
  ADD(&gden, pv, &q);
  if (ISZERO(&gden)) { decContextZeroStatus(&dec_ctx); return mm_error("?n: no solution"); }
  DIV(&g, &gnum, &gden);
  if (ISNEG(&g) || ISZERO(&g)) { decContextZeroStatus(&dec_ctx); return mm_error("?n: no solution (non-positive growth)"); }
  LN(&lng, &g); onep(&o, ipct); LN(&ln1, &o);
  DIV(out, &lng, &ln1);
  return dec_check("?n");
}

/* ---- residuals and the rate solver: scan a percent grid for a sign change,
   then bisect in decimal ---- */
typedef int (*resfn)(const mm_dec *ipct, mm_dec *out, void *ud);

struct tvm_args { const mm_dec *n, *ipct, *pv, *pmt, *fv, *gpct; int begin; };

/* pv (1+r)^n + pmt k A + fv, the TVM equation with growth */
static void tvm_eq(const struct tvm_args *a, const mm_dec *n, const mm_dec *ipct, const mm_dec *gpct, mm_dec *out) {
  mm_dec r, g, A, k, t1, t2, k_A, o, gr;
  frac_of(&r, ipct); frac_of(&gr, gpct); onep(&o, ipct); POW(&g, &o, n);
  ann_factor(&A, &g, &o, &r, &gr, n); due_factor(&k, &r, a->begin);
  MUL(&t1, a->pv, &g);
  MUL(&k_A, &k, &A); MUL(&t2, a->pmt, &k_A);
  ADD(out, &t1, &t2); ADD(out, out, a->fv);
}
static int tvm_residual(const mm_dec *ipct, mm_dec *out, void *ud) {
  struct tvm_args *a = ud; tvm_eq(a, a->n, ipct, a->gpct, out); return 0;
}
static int tvm_residual_g(const mm_dec *gpct, mm_dec *out, void *ud) {
  struct tvm_args *a = ud; tvm_eq(a, a->n, a->ipct, gpct, out); return 0;
}
static int tvm_residual_n(const mm_dec *n, mm_dec *out, void *ud) {
  struct tvm_args *a = ud; tvm_eq(a, n, a->ipct, a->gpct, out); return 0;
}

struct cf_args { const CashFlow *cf; int ncf; };

static int npv_residual(const mm_dec *ipct, mm_dec *out, void *ud) {
  struct cf_args *a = ud;
  mm_dec r, one, base, disc, acc, term, texp;
  long t = 0;
  frac_of(&r, ipct); INT(&one, 1); ADD(&base, &one, &r);
  INT(&acc, 0);
  for (int j = 0; j < a->ncf; j++)
    for (long c = 0; c < a->cf[j].count; c++, t++) {
      INT(&texp, t); POW(&disc, &base, &texp);
      DIV(&term, &a->cf[j].amt, &disc);
      ADD(&acc, &acc, &term);
    }
  CPY(out, &acc);
  return 0;
}

static int sign_of(resfn f, void *ud, const mm_dec *x) {
  mm_dec r;
  if (f(x, &r, ud) != 0 || decNumberIsNaN(&r) || decNumberIsInfinite(&r)) return 2;   /* no value here */
  if (ISZERO(&r)) return 0;
  return decNumberIsNegative(&r) ? -1 : 1;
}

/* The root between xa (where the sign of f is sa) and xb (where it is the
 * other). False position with the Illinois correction: the chord's crossing
 * is the next point, and an end that is kept twice running has its value
 * halved so that it cannot stall. Each step is one evaluation, and it takes
 * about twenty to reach 34 digits where bisection takes over a hundred,
 * which matters when one evaluation is eighteen 34-digit powers. A step that
 * cannot be taken (no value there, or it falls outside the bracket) is a
 * bisection step instead, so the bracket always shrinks. */
static void bisect(resfn f, void *ud, mm_dec *xa, int sa, mm_dec *xb, mm_dec *root) {
  mm_dec fa, fb, c, fc, num, den, half, width, tol, scale, one;
  decNumberFromString(&half, "0.5", &dec_ctx);
  decNumberFromString(&tol, "1E-31", &dec_ctx);
  INT(&one, 1);
  int have = f(xa, &fa, ud) == 0 && f(xb, &fb, ud) == 0 && !decNumberIsNaN(&fa) && !decNumberIsNaN(&fb)
          && !decNumberIsInfinite(&fa) && !decNumberIsInfinite(&fb);
  int side = 0;
  for (int it = 0; it < 250; it++) {
    SUB(&width, xb, xa); ABS(&width, &width);
    ABS(&scale, xa); if (dec_compare(&scale, &one) < 0) CPY(&scale, &one);
    MUL(&scale, &scale, &tol);
    if (dec_compare(&width, &scale) <= 0) break;       /* the two ends agree to 31 digits */
    int chord = 0;
    if (have) {                                        /* c = xb - fb (xb - xa) / (fb - fa) */
      SUB(&den, &fb, &fa);
      if (!ISZERO(&den)) {
        SUB(&num, xb, xa); MUL(&num, &num, &fb); DIV(&num, &num, &den); SUB(&c, xb, &num);
        mm_dec lo, hi;
        if (dec_compare(xa, xb) < 0) { CPY(&lo, xa); CPY(&hi, xb); } else { CPY(&lo, xb); CPY(&hi, xa); }
        chord = !decNumberIsNaN(&c) && dec_compare(&c, &lo) > 0 && dec_compare(&c, &hi) < 0;
      }
    }
    if (!chord) { ADD(&c, xa, xb); MUL(&c, &c, &half); }
    int ok = f(&c, &fc, ud) == 0 && !decNumberIsNaN(&fc) && !decNumberIsInfinite(&fc);
    if (!ok) break;
    if (ISZERO(&fc)) { CPY(root, &c); decContextZeroStatus(&dec_ctx); return; }
    int sc = decNumberIsNegative(&fc) ? -1 : 1;
    if (sc == sa) {
      CPY(xa, &c); CPY(&fa, &fc);
      if (side == -1 && have) MUL(&fb, &fb, &half);
      side = -1;
    } else {
      CPY(xb, &c); CPY(&fb, &fc);
      if (side == 1 && have) MUL(&fa, &fa, &half);
      side = 1;
    }
  }
  ADD(root, xa, xb); MUL(root, root, &half);
  decContextZeroStatus(&dec_ctx);           /* the search may have raised flags at extreme rates */
}

/* Scan [lo, hi] in steps for a sign change of f, then bisect. */
static int scan_bisect(const char *what, resfn f, void *ud, double lo, double hi, double step, const char *range, mm_dec *root) {
  mm_dec xa, xb;
  char buf[32];
  decContextZeroStatus(&dec_ctx);
  snprintf(buf, sizeof buf, "%.6f", lo);
  decNumberFromString(&xa, buf, &dec_ctx);
  int sa = sign_of(f, ud, &xa);
  if (sa == 0) { CPY(root, &xa); decContextZeroStatus(&dec_ctx); return 0; }
  int found = 0;
  for (double b = lo + step; b <= hi + 1e-9; b += step) {
    snprintf(buf, sizeof buf, "%.6f", b);
    decNumberFromString(&xb, buf, &dec_ctx);
    int sb = sign_of(f, ud, &xb);
    if (sb == 0) { CPY(root, &xb); decContextZeroStatus(&dec_ctx); return 0; }
    if (sb == 2) continue;                                 /* undefined here: keep scanning */
    if (sa != 2 && sb != sa) { found = 1; break; }
    CPY(&xa, &xb); sa = sb;
  }
  if (!found) { decContextZeroStatus(&dec_ctx); return mm_error("%s: no %s", what, range); }
  bisect(f, ud, &xa, sa, &xb, root);
  return 0;
}

/* A rate for a residual that only falls, or only rises, across the range (a
 * bond's price against its yield). The signs at the two ends say at once
 * whether there is a root, and bisection between them finds it: about 120
 * evaluations. Scanning the range in quarter-percent steps for the first
 * change of sign, as scan_bisect must for a residual that may turn (an IRR
 * with several roots), took 4400 evaluations in 34-digit arithmetic: two
 * seconds for a bond with eighteen coupons, and fourteen before a mistyped
 * price was told it has no yield. If the ends agree, a coarse scan still
 * looks for a root that inputs outside finance might hide. */
static int solve_rate_monotone(const char *what, resfn f, void *ud, mm_dec *root_pct) {
  mm_dec xa, xb;
  decContextZeroStatus(&dec_ctx);
  decNumberFromString(&xa, "-99", &dec_ctx);
  decNumberFromString(&xb, "1000", &dec_ctx);
  int sa = sign_of(f, ud, &xa), sb = sign_of(f, ud, &xb);
  if (sa == 0) { CPY(root_pct, &xa); decContextZeroStatus(&dec_ctx); return 0; }
  if (sb == 0) { CPY(root_pct, &xb); decContextZeroStatus(&dec_ctx); return 0; }
  if (sa == 2 || sb == 2 || sa == sb)
    return scan_bisect(what, f, ud, -99.0, 1000.0, 5.0, "rate found between -99% and 1000% per period", root_pct);
  bisect(f, ud, &xa, sa, &xb, root_pct);
  return 0;
}

static int solve_rate(const char *what, resfn f, void *ud, mm_dec *root_pct) {
  return scan_bisect(what, f, ud, -99.0, 1000.0, 0.25, "rate found between -99% and 1000% per period", root_pct);
}

int fin_tvm_i(const mm_dec *n, const mm_dec *pv, const mm_dec *pmt, const mm_dec *fv, const mm_dec *gpct, int begin, mm_dec *out) {
  struct tvm_args a = { n, NULL, pv, pmt, fv, gpct, begin };
  if (ISZERO(n)) return mm_error("?i%%: n is zero");
  return solve_rate("?i%", tvm_residual, &a, out);
}

int fin_tvm_g(const mm_dec *n, const mm_dec *ipct, const mm_dec *pv, const mm_dec *pmt, const mm_dec *fv, int begin, mm_dec *out) {
  struct tvm_args a = { n, ipct, pv, pmt, fv, NULL, begin };
  if (ISZERO(n)) return mm_error("?g%%: n is zero");
  if (ISZERO(pmt)) return mm_error("?g%%: pmt is zero, so growth has nothing to act on");
  return solve_rate("?g%", tvm_residual_g, &a, out);
}

/* n for a growing payment with fv != 0: two exponentials in n, so scan whole periods up to 10000 and bisect */
static int solve_n_growing(const mm_dec *ipct, const mm_dec *pv, const mm_dec *pmt, const mm_dec *fv, const mm_dec *gpct, int begin, mm_dec *out) {
  struct tvm_args a = { NULL, ipct, pv, pmt, fv, gpct, begin };
  if (ISZERO(pmt)) return mm_error("?n: pmt is zero");
  return scan_bisect("?n", tvm_residual_n, &a, 0.0, 10000.0, 1.0, "number of periods found between 0 and 10000", out);
}

/* ---- NPV / IRR ---- */
int fin_npv(const CashFlow *cf, int ncf, const mm_dec *ipct, mm_dec *out) {
  struct cf_args a = { cf, ncf };
  if (ncf == 0) return mm_error("npv: no cash flows");
  decContextZeroStatus(&dec_ctx);
  npv_residual(ipct, out, &a);
  return dec_check("npv");
}

int fin_irr(const CashFlow *cf, int ncf, mm_dec *out) {
  struct cf_args a = { cf, ncf };
  long total = 0;
  for (int j = 0; j < ncf; j++) total += cf[j].count;
  if (total < 2) return mm_error("irr: need at least an outlay and a return");
  return solve_rate("irr", npv_residual, &a, out);
}

/* |x| < 1E-20 |ref|: a residue of 34-digit arithmetic, not a balance */
static int dec_compare_abs_tiny(const mm_dec *x, const mm_dec *ref) {
  mm_dec ax, ar, eps, lim;
  ABS(&ax, x); ABS(&ar, ref);
  decNumberFromString(&eps, "1E-20", &dec_ctx);
  MUL(&lim, &ar, &eps);
  return dec_compare(&ax, &lim) < 0;
}

/* ---- amortisation: end-of-period, magnitudes ---- */
int fin_amort_rows(AmortRow **rows, long *nrows, mm_dec *tot_interest, mm_dec *tot_principal) {
  bool ok;
  long N = dec_to_long(&T_n, &ok);
  if (!ok || N < 1) return mm_error("amort: n must be a whole number of periods >= 1 (set it with `n`)");
  if (N > 100000) return mm_error("amort: n is too large");
  if (ISZERO(&T_pmt)) return mm_error("amort: set pmt first (or solve it with ?pmt)");
  AmortRow *r = malloc((size_t)N * sizeof *r);
  if (!r) return mm_error("amort: out of memory");
  mm_dec bal, rate, pay, interest, principal, totI, totP, grow;
  decContextZeroStatus(&dec_ctx);
  ABS(&bal, &T_pv); frac_of(&rate, &T_i); ABS(&pay, &T_pmt); onep(&grow, &T_g);
  INT(&totI, 0); INT(&totP, 0);
  for (long p = 0; p < N; p++) {
    if (p > 0) MUL(&pay, &pay, &grow);                     /* pmt (1+g)^(p) */
    MUL(&interest, &bal, &rate);
    SUB(&principal, &pay, &interest);
    SUB(&bal, &bal, &principal);
    if (dec_compare_abs_tiny(&bal, &T_pv)) decNumberZero(&bal);   /* 1E-31 left by a rounded pmt is zero */
    ADD(&totI, &totI, &interest);
    ADD(&totP, &totP, &principal);
    r[p].payment = pay; r[p].interest = interest; r[p].principal = principal; r[p].balance = bal;
  }
  if (dec_check("amort") != 0) { free(r); return -1; }
  *rows = r; *nrows = N; *tot_interest = totI; *tot_principal = totP;
  return 0;
}

/* ---- interest-rate conversions ---- */
int fin_nom2eff(const mm_dec *nompct, const mm_dec *m, mm_dec *out) {
  mm_dec h, one, base, p, t;
  decContextZeroStatus(&dec_ctx);
  INT(&h, 100); INT(&one, 1);
  DIV(&base, nompct, &h); DIV(&base, &base, m); ADD(&base, &base, &one);
  POW(&p, &base, m); SUB(&t, &p, &one); MUL(out, &t, &h);
  return dec_check("nom>eff");
}

int fin_eff2nom(const mm_dec *effpct, const mm_dec *m, mm_dec *out) {
  mm_dec h, one, base, inv, p, t;
  decContextZeroStatus(&dec_ctx);
  INT(&h, 100); INT(&one, 1);
  DIV(&base, effpct, &h); ADD(&base, &base, &one);
  DIV(&inv, &one, m); POW(&p, &base, &inv); SUB(&t, &p, &one);
  MUL(&t, &t, m); MUL(out, &t, &h);
  return dec_check("eff>nom");
}

/* ---- depreciation ---- */
int fin_sl(const mm_dec *cost, const mm_dec *salv, const mm_dec *life, mm_dec *out) {
  mm_dec d;
  decContextZeroStatus(&dec_ctx);
  SUB(&d, cost, salv); DIV(out, &d, life);
  return dec_check("sl");
}

int fin_soyd(const mm_dec *cost, const mm_dec *salv, const mm_dec *life, const mm_dec *year, mm_dec *out) {
  mm_dec base, one, two, rem, sum, t;
  decContextZeroStatus(&dec_ctx);
  SUB(&base, cost, salv); INT(&one, 1); INT(&two, 2);
  SUB(&rem, life, year); ADD(&rem, &rem, &one);          /* life - year + 1 */
  ADD(&sum, life, &one); MUL(&sum, &sum, life); DIV(&sum, &sum, &two);  /* life(life+1)/2 */
  MUL(&t, &base, &rem); DIV(out, &t, &sum);
  return dec_check("soyd");
}

int fin_db(const mm_dec *cost, const mm_dec *salv, const mm_dec *life, const mm_dec *year, const mm_dec *factor, mm_dec *out) {
  mm_dec one, rate, ym1, base, o, bookstart, dep, floor_;
  decContextZeroStatus(&dec_ctx);
  INT(&one, 1);
  DIV(&rate, factor, life);                                /* factor/life */
  SUB(&ym1, year, &one);
  SUB(&base, &one, &rate); POW(&o, &base, &ym1);
  MUL(&bookstart, cost, &o);                               /* cost (1-rate)^(year-1) */
  MUL(&dep, &bookstart, &rate);
  SUB(&floor_, &bookstart, salv);                          /* never below salvage */
  if (ISNEG(&floor_)) INT(&floor_, 0);
  if (dec_compare(&dep, &floor_) > 0) CPY(&dep, &floor_);
  CPY(out, &dep);
  return dec_check("db");
}

/* ---- bonds, settlement on a coupon date (no accrued interest) ---- */
int fin_bond_price(const mm_dec *cpnpct, const mm_dec *yldpct, const mm_dec *nper, const mm_dec *redeem, mm_dec *out) {
  mm_dec y, g, A, t1, t2, o;
  decContextZeroStatus(&dec_ctx);
  frac_of(&y, yldpct);
  onep(&o, yldpct); POW(&g, &o, nper);                     /* (1+y)^n */
  pv_ann_factor(&A, &g, &y, nper);
  MUL(&t1, cpnpct, &A);                                    /* coupon per 100 face */
  DIV(&t2, redeem, &g);
  ADD(out, &t1, &t2);
  return dec_check("bprice");
}

struct bond_args { const mm_dec *cpn, *nper, *redeem, *price; };

static int bond_residual(const mm_dec *yldpct, mm_dec *out, void *ud) {
  struct bond_args *b = ud;
  mm_dec y, g, A, t1, t2, o;
  frac_of(&y, yldpct); onep(&o, yldpct); POW(&g, &o, b->nper);
  pv_ann_factor(&A, &g, &y, b->nper);
  MUL(&t1, b->cpn, &A); DIV(&t2, b->redeem, &g); ADD(out, &t1, &t2);
  SUB(out, out, b->price);
  return 0;
}

int fin_bond_yield(const mm_dec *price, const mm_dec *cpnpct, const mm_dec *nper, const mm_dec *redeem, mm_dec *out) {
  struct bond_args b = { cpnpct, nper, redeem, price };
  return solve_rate_monotone("byield", bond_residual, &b, out);
}

/* ---- dated cash flows: XNPV discounts actual/365 from the first date ---- */
struct xcf_args { const DatedFlow *cf; int ncf; };

static int xnpv_residual(const mm_dec *ratepct, mm_dec *out, void *ud) {
  struct xcf_args *a = ud;
  mm_dec r, one, base, acc, term, expo, df, daysn, t365;
  frac_of(&r, ratepct);
  INT(&one, 1); ADD(&base, &one, &r);
  INT(&acc, 0); INT(&t365, 365);
  long d0 = a->cf[0].serial;
  for (int i = 0; i < a->ncf; i++) {
    INT(&daysn, a->cf[i].serial - d0); DIV(&expo, &daysn, &t365);
    POW(&df, &base, &expo);
    DIV(&term, &a->cf[i].amt, &df);
    ADD(&acc, &acc, &term);
  }
  CPY(out, &acc);
  return 0;
}

int fin_xnpv(const DatedFlow *cf, int ncf, const mm_dec *ratepct, mm_dec *out) {
  struct xcf_args a = { cf, ncf };
  if (ncf == 0) return mm_error("xnpv: no cash flows");
  decContextZeroStatus(&dec_ctx);
  xnpv_residual(ratepct, out, &a);
  return dec_check("xnpv");
}

int fin_xirr(const DatedFlow *cf, int ncf, mm_dec *out) {
  struct xcf_args a = { cf, ncf };
  if (ncf < 2) return mm_error("xirr: need at least an outlay and a return");
  return solve_rate("xirr", xnpv_residual, &a, out);
}

/* ---- dated bonds (SIA / Excel conventions), clean price per 100 face ---- */
static int xbond_price_raw(const char *what, long settle, long maturity, const mm_dec *ratepct, const mm_dec *yldpct,
                           const mm_dec *redeem, int freq, int basis, mm_dec *out) {
  int N; long A, E;
  if (fin_coupon_geometry(what, settle, maturity, freq, basis, &N, &A, &E) != 0) return -1;
  if (E == 0) return mm_error("%s: degenerate coupon period", what);
  mm_dec frq, cpn, yper, one, dbase, An, En, DSCn, t1, accr_frac, accrued, clean;
  INT(&frq, freq); INT(&one, 1);
  DIV(&cpn, ratepct, &frq);                                /* coupon per period per 100 face */
  frac_of(&yper, yldpct); DIV(&yper, &yper, &frq);
  ADD(&dbase, &one, &yper);                                /* 1 + y/freq */
  INT(&An, A); INT(&En, E);
  SUB(&DSCn, &En, &An); DIV(&t1, &DSCn, &En);              /* DSC/E */
  DIV(&accr_frac, &An, &En);                               /* A/E */
  MUL(&accrued, &cpn, &accr_frac);
  if (N == 1) {                                            /* final period: simple interest */
    mm_dec denom, tmp;
    MUL(&tmp, &t1, &yper); ADD(&denom, &one, &tmp);
    ADD(&clean, redeem, &cpn); DIV(&clean, &clean, &denom);
    SUB(&clean, &clean, &accrued);
  } else {
    mm_dec sum, kk, expo, df, term, rexpo, rdf, rpv;
    INT(&sum, 0);
    for (int k = 1; k <= N; k++) {
      INT(&kk, k - 1); ADD(&expo, &kk, &t1);
      POW(&df, &dbase, &expo);
      DIV(&term, &cpn, &df);
      ADD(&sum, &sum, &term);
    }
    INT(&rexpo, N - 1); ADD(&rexpo, &rexpo, &t1);
    POW(&rdf, &dbase, &rexpo);
    DIV(&rpv, redeem, &rdf);
    ADD(&clean, &sum, &rpv);
    SUB(&clean, &clean, &accrued);
  }
  CPY(out, &clean);
  return 0;
}

int fin_xbond_price(long settle, long maturity, const mm_dec *ratepct, const mm_dec *yldpct,
                    const mm_dec *redeem, int freq, int basis, mm_dec *out) {
  decContextZeroStatus(&dec_ctx);
  if (xbond_price_raw("xprice", settle, maturity, ratepct, yldpct, redeem, freq, basis, out) != 0) return -1;
  return dec_check("xprice");
}

struct xbond_args { long settle, maturity; const mm_dec *ratepct, *redeem, *price; int freq, basis; };

static int xbond_residual(const mm_dec *yldpct, mm_dec *out, void *ud) {
  struct xbond_args *b = ud;
  mm_dec pr;
  if (xbond_price_raw("xyield", b->settle, b->maturity, b->ratepct, yldpct, b->redeem, b->freq, b->basis, &pr) != 0) return -1;
  SUB(out, &pr, b->price);
  return 0;
}

int fin_xbond_yield(long settle, long maturity, const mm_dec *ratepct, const mm_dec *price,
                    const mm_dec *redeem, int freq, int basis, mm_dec *out) {
  int N; long A, E;
  if (fin_coupon_geometry("xyield", settle, maturity, freq, basis, &N, &A, &E) != 0) return -1;  /* validate once, with messages */
  if (E == 0) return mm_error("xyield: degenerate coupon period");
  struct xbond_args b = { settle, maturity, ratepct, redeem, price, freq, basis };
  return solve_rate_monotone("xyield", xbond_residual, &b, out);
}

int fin_xaccrued(long settle, long maturity, const mm_dec *ratepct, int freq, int basis, mm_dec *out) {
  int N; long A, E;
  if (fin_coupon_geometry("xaccr", settle, maturity, freq, basis, &N, &A, &E) != 0) return -1;
  if (E == 0) return mm_error("xaccr: degenerate coupon period");
  mm_dec frq, cpn, An, En, frac;
  decContextZeroStatus(&dec_ctx);
  INT(&frq, freq); DIV(&cpn, ratepct, &frq);
  INT(&An, A); INT(&En, E); DIV(&frac, &An, &En);
  MUL(out, &cpn, &frac);
  return dec_check("xaccr");
}
