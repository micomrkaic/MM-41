/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The finance module's internals: the numerical kernel (fin_core.c) and the
 * calendar it needs (fin_dates.c). Everything computes in decimals.
 *
 * Periodic model: i is the per-period rate in PERCENT, n the number of
 * periods. Sign convention: cash received +, cash paid -. The TVM identity,
 * with type = 1 when payments fall at the start of the period:
 *
 *     PV*(1+r)^n + PMT*(1+r*type)*[((1+r)^n - 1)/r] + FV = 0,   r = i/100
 *
 * The kernel was ported from MM-B1, the decimal business calculator. */

#ifndef MM41_FIN_H
#define MM41_FIN_H

#include "decimal.h"

/* ---- TVM registers (module state) ---- */
extern mm_dec T_n, T_i, T_pv, T_pmt, T_fv;
extern int    T_begin;
void fin_tvm_reset(void);

/* ---- TVM closed forms and the rate solver; 0 ok, -1 error reported ---- */
int fin_tvm_fv (const mm_dec *n, const mm_dec *ipct, const mm_dec *pv,  const mm_dec *pmt, int begin, mm_dec *out);
int fin_tvm_pv (const mm_dec *n, const mm_dec *ipct, const mm_dec *pmt, const mm_dec *fv,  int begin, mm_dec *out);
int fin_tvm_pmt(const mm_dec *n, const mm_dec *ipct, const mm_dec *pv,  const mm_dec *fv,  int begin, mm_dec *out);
int fin_tvm_n  (const mm_dec *ipct, const mm_dec *pv, const mm_dec *pmt, const mm_dec *fv, int begin, mm_dec *out);
int fin_tvm_i  (const mm_dec *n, const mm_dec *pv, const mm_dec *pmt, const mm_dec *fv,   int begin, mm_dec *out);

/* ---- cash flows: amounts in time order, each repeated count times ---- */
typedef struct { mm_dec amt; long count; } CashFlow;
int fin_npv(const CashFlow *cf, int ncf, const mm_dec *ipct, mm_dec *out);
int fin_irr(const CashFlow *cf, int ncf, mm_dec *out);

/* dated cash flows: amount and serial day (see fin_dates) */
typedef struct { mm_dec amt; long serial; } DatedFlow;
int fin_xnpv(const DatedFlow *cf, int ncf, const mm_dec *ratepct, mm_dec *out);
int fin_xirr(const DatedFlow *cf, int ncf, mm_dec *out);

/* ---- amortisation of the TVM registers: one row per period ---- */
typedef struct { mm_dec payment, interest, principal, balance; } AmortRow;
int fin_amort_rows(AmortRow **rows, long *nrows, mm_dec *tot_interest, mm_dec *tot_principal);

/* ---- rates, depreciation, bonds ---- */
int fin_nom2eff(const mm_dec *nompct, const mm_dec *m, mm_dec *out);
int fin_eff2nom(const mm_dec *effpct, const mm_dec *m, mm_dec *out);
int fin_sl  (const mm_dec *cost, const mm_dec *salv, const mm_dec *life, mm_dec *out);
int fin_soyd(const mm_dec *cost, const mm_dec *salv, const mm_dec *life, const mm_dec *year, mm_dec *out);
int fin_db  (const mm_dec *cost, const mm_dec *salv, const mm_dec *life, const mm_dec *year, const mm_dec *factor, mm_dec *out);
int fin_bond_price(const mm_dec *cpnpct, const mm_dec *yldpct, const mm_dec *nper, const mm_dec *redeem, mm_dec *out);
int fin_bond_yield(const mm_dec *price, const mm_dec *cpnpct, const mm_dec *nper, const mm_dec *redeem, mm_dec *out);
int fin_xbond_price(long settle, long maturity, const mm_dec *ratepct, const mm_dec *yldpct,
                    const mm_dec *redeem, int freq, int basis, mm_dec *out);
int fin_xbond_yield(long settle, long maturity, const mm_dec *ratepct, const mm_dec *price,
                    const mm_dec *redeem, int freq, int basis, mm_dec *out);
int fin_xaccrued(long settle, long maturity, const mm_dec *ratepct, int freq, int basis, mm_dec *out);

/* ---- day counts (fin_dates.c), on the serial days of include/calendar.h ----
 * Day-count basis as in Excel: 0 = 30/360 US, 1 = actual/actual (ISDA),
 * 2 = actual/360, 3 = actual/365, 4 = 30E/360. */
int  fin_year_frac(long s1, long s2, int basis, mm_dec *out);
int  fin_coupon_geometry(const char *what, long settle, long maturity, int freq, int basis, int *N, long *A, long *E);

#endif
