/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The calendar core shared by the dates and finance modules: proleptic
 * Gregorian dates as serial days (epoch 1970-01-01; every consumer uses
 * differences only), Howard Hinnant's branch-free civil<->days algorithm,
 * and the two spellings of a date on the stack: the string "d.m.yyyy" of
 * the dates module and the integer YYYYMMDD of the finance module. */

#ifndef MM41_CALENDAR_H
#define MM41_CALENDAR_H

#include "stack.h"
#include <stdbool.h>

bool cal_is_leap(long y);
int  cal_days_in_month(long y, int m);
long cal_days_in_year(long y);

long cal_civil_to_serial(long y, int m, int d);           /* no validation */
void cal_serial_to_civil(long serial, long *y, int *m, int *d);
int  cal_dow(long serial);                                /* 0 = Sunday .. 6 = Saturday */
bool cal_is_weekend(long serial);

/* validated conversions; -1 and an error naming `what` when invalid */
int  cal_dmy_to_serial(const char *what, int d, int m, long y, long *serial);
int  cal_ymd_to_serial(const char *what, long ymd, long *serial);
long cal_serial_to_ymd(long serial);
void cal_serial_to_string(long serial, char *buf, size_t n);  /* "dd.mm.yyyy" */

/* A date on the stack: a string "d.m.yyyy" or a real/decimal YYYYMMDD. */
bool cal_elem_is_date(const stack_element *e);
int  cal_elem_to_serial(const stack_element *e, const char *what, long *serial);
int  cal_parse_date_string(const char *s, long *serial);  /* -1 if not "d.m.yyyy" or YYYYMMDD */

long cal_easter_serial(long y);                           /* Gregorian Easter Sunday */
long cal_today_serial(void);

#endif
