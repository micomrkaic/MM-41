/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#include "calendar.h"
#include "decimal.h"
#include "errors.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool cal_is_leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int cal_days_in_month(long y, int m) {
  static const int t[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  if (m == 2 && cal_is_leap(y)) return 29;
  return t[m - 1];
}

long cal_days_in_year(long y) { return cal_is_leap(y) ? 366 : 365; }

long cal_civil_to_serial(long y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void cal_serial_to_civil(long z, long *yy, int *mm, int *dd) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = yoe + era * 400;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  int d = (int)(doy - (153 * mp + 2) / 5 + 1);
  int m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *yy = y + (m <= 2);
  *mm = m;
  *dd = d;
}

int cal_dow(long serial) {                 /* 1970-01-01 was a Thursday */
  long w = (serial % 7 + 4) % 7;
  if (w < 0) w += 7;
  return (int)w;
}

bool cal_is_weekend(long serial) { int w = cal_dow(serial); return w == 0 || w == 6; }

int cal_dmy_to_serial(const char *what, int d, int m, long y, long *serial) {
  if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || d > cal_days_in_month(y, m))
    return mm_error("%s: invalid date %d.%d.%ld", what, d, m, y);
  *serial = cal_civil_to_serial(y, m, d);
  return 0;
}

int cal_ymd_to_serial(const char *what, long ymd, long *serial) {
  long y = ymd / 10000;
  int m = (int)((ymd / 100) % 100), d = (int)(ymd % 100);
  if (ymd < 10101 || y > 9999 || m < 1 || m > 12 || d < 1 || d > cal_days_in_month(y, m))
    return mm_error("%s: invalid date %ld (use YYYYMMDD or \"d.m.yyyy\")", what, ymd);
  *serial = cal_civil_to_serial(y, m, d);
  return 0;
}

long cal_serial_to_ymd(long serial) {
  long y; int m, d;
  cal_serial_to_civil(serial, &y, &m, &d);
  return y * 10000 + m * 100 + d;
}

void cal_serial_to_string(long serial, char *buf, size_t n) {
  long y; int m, d;
  cal_serial_to_civil(serial, &y, &m, &d);
  snprintf(buf, n, "%02d.%02d.%ld", d, m, y);
}

bool cal_elem_is_date(const stack_element *e) {
  return e->type == TYPE_STRING || e->type == TYPE_REAL || e->type == TYPE_DECIMAL;
}

/* "d.m.yyyy" (leading zeros optional) or the digits YYYYMMDD; surrounding
   whitespace allowed. -1 and no message when the text is neither. */
int cal_parse_date_string(const char *s, long *serial) {
  int d, m; long y; int n = 0;
  if (sscanf(s, " %d.%d.%ld %n", &d, &m, &y, &n) == 3 && s[n] == '\0') {
    if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || d > cal_days_in_month(y, m)) return -1;
    *serial = cal_civil_to_serial(y, m, d);
    return 0;
  }
  long ymd; n = 0;
  if (sscanf(s, " %ld %n", &ymd, &n) == 1 && s[n] == '\0' && ymd >= 10101 && ymd <= 99991231) {
    long yy = ymd / 10000; int mm = (int)((ymd / 100) % 100), dd = (int)(ymd % 100);
    if (mm < 1 || mm > 12 || dd < 1 || dd > cal_days_in_month(yy, mm)) return -1;
    *serial = cal_civil_to_serial(yy, mm, dd);
    return 0;
  }
  return -1;
}

int cal_elem_to_serial(const stack_element *e, const char *what, long *serial) {
  if (e->type == TYPE_STRING) {
    const char *s = e->string;
    int d, m; long y, ymd; int n = 0;
    if (sscanf(s, " %d.%d.%ld %n", &d, &m, &y, &n) == 3 && s[n] == '\0')
      return cal_dmy_to_serial(what, d, m, y, serial);
    n = 0;
    if (sscanf(s, " %ld %n", &ymd, &n) == 1 && s[n] == '\0')
      return cal_ymd_to_serial(what, ymd, serial);
    return mm_error("%s: a date is \"d.m.yyyy\" or the number YYYYMMDD, not \"%s\"", what, s);
  }
  if (e->type == TYPE_REAL || e->type == TYPE_DECIMAL) {
    double x = e->type == TYPE_REAL ? e->real : dec_to_double(&e->dec);
    if (!(x >= 10101 && x <= 99991231) || x != (double)(long)x)
      return mm_error("%s: a numeric date is the whole number YYYYMMDD", what);
    return cal_ymd_to_serial(what, (long)x, serial);
  }
  return mm_error("%s: a date is \"d.m.yyyy\" or the number YYYYMMDD", what);
}

/* Anonymous Gregorian algorithm (Meeus/Jones/Butcher). */
long cal_easter_serial(long y) {
  long a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4;
  long f = (b + 8) / 25, g = (b - f + 1) / 3, h = (19 * a + b - d - g + 15) % 30;
  long i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
  long m = (a + 11 * h + 22 * l) / 451;
  long month = (h + l - 7 * m + 114) / 31, day = (h + l - 7 * m + 114) % 31 + 1;
  return cal_civil_to_serial(y, (int)month, (int)day);
}

long cal_today_serial(void) {
  time_t t = time(NULL);
  struct tm *lt = localtime(&t);
  return cal_civil_to_serial(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}
