/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#include "module.h"
#include "errors.h"
#include "stack.h"
#include "my_date_fun.h"
#include "my_astronomy.h"
#include "workdays.h"


static int op_today(Stack *stack) { (void)stack; (void)(push_today_date(stack)); return 0; }
static int op_now(Stack *stack) { (void)stack; (void)(push_now_time(stack)); return 0; }
static int op_tz_offset(Stack *stack) { (void)stack; (void)(tz_offset(stack)); return 0; }
static int op_ddays(Stack *stack) { (void)stack; (void)(delta_days_strings(stack)); return 0; }
static int op_dateplus(Stack *stack) { (void)stack; (void)(date_plus_days(stack)); return 0; }
static int op_dow(Stack *stack) { (void)stack; (void)(push_weekday_name_from_date_string(stack)); return 0; }
static int op_edmy(Stack *stack) { (void)stack; (void)(extract_day_month_year(stack)); return 0; }
static int op_num2date(Stack *stack) { (void)stack; (void)(make_date_string(stack)); return 0; }
static int op_days2eoy(Stack *stack) { (void)stack; (void)(days_to_end_of_year(stack)); return 0; }
static int op_sunrise(Stack *stack) { (void)stack; (void)(sunrise(stack)); return 0; }
static int op_sunset(Stack *stack) { (void)stack; (void)(sunset(stack)); return 0; }
static int op_dawn(Stack *stack) { (void)stack; (void)(dawn(stack)); return 0; }
static int op_dusk(Stack *stack) { (void)stack; (void)(dusk(stack)); return 0; }

static const Op table[] = {
  { "today", 0, op_today, "Dates", "-- date", "today's date as a string d.m.yyyy", "today" },
  { "now", 0, op_now, "Dates", "-- \"HH:MM\"", "push the current local time as a string, same form as sunrise/sunset", "now" },
  { "tz_offset", 2, op_tz_offset, "Dates", "date zone -- hours", "UTC offset in hours of an IANA zone on the given date, DST included", "\"1.12.2026\" \"America/New_York\" tz_offset" },
  { "ddays", 2, op_ddays, "Dates", "d1 d2 -- n", "days from date d1 to date d2 (d2 - d1)", "\"1.1.2025\" \"31.12.2025\" ddays" },
  { "dateplus", 2, op_dateplus, "Dates", "date n -- date2", "the date n days later (n may be negative)", "\"1.1.2025\" 30 dateplus" },
  { "dow", 1, op_dow, "Dates", "date -- name", "weekday name of a date", "\"1.1.2025\" dow" },
  { "edmy", 1, op_edmy, "Dates", "date -- y m d", "split a date into year, month, day (day on top)", "\"1.1.2025\" edmy" },
  { "num2date", 3, op_num2date, "Dates", "d m y -- date", "build a date string from day, month, year", "1 1 2025 num2date" },
  { "days2eoy", 1, op_days2eoy, "Dates", "date -- n", "days from the date to the end of its year", "\"1.1.2025\" days2eoy" },
  { "sunrise", 4, op_sunrise, "Dates", "date lat lon zone -- time", "local sunrise HH:MM; zone is an IANA name or a UTC offset in hours", "\"21.6.2025\" 38.9 -77.0 \"America/New_York\" sunrise" },
  { "sunset", 4, op_sunset, "Dates", "date lat lon zone -- time", "local sunset HH:MM", "\"21.6.2025\" 38.9 -77.0 -4 sunset" },
  { "dawn", 4, op_dawn, "Dates", "date lat lon zone -- time", "civil dawn HH:MM (sun 6 degrees below the horizon)", "today 38.9 -77.0 \"America/New_York\" dawn" },
  { "dusk", 4, op_dusk, "Dates", "date lat lon zone -- time", "civil dusk HH:MM", "today 38.9 -77.0 \"America/New_York\" dusk" },
  { "workdays", 2, op_workdays, "Workdays", "d1 d2 -- n", "workdays from d1 to d2, both inclusive (Excel NETWORKDAYS): Mondays to Fridays not in the holiday calendar; negative if d2 < d1", "\"1.1.2026\" \"31.12.2026\" workdays" },
  { "workdayplus", 2, op_workdayplus, "Workdays", "date n -- date2", "the date n workdays after date (n may be negative; the start date is not counted, as Excel WORKDAY)", "\"24.12.2026\" 10 workdayplus" },
  { "isworkday", 1, op_isworkday, "Workdays", "date -- flag", "1 if the date is a Monday to Friday and not a holiday", "20261126 isworkday" },
  { "holidays", 1, op_holidays, "Workdays", "name -- H", "the holiday calendar as a vector of YYYYMMDD: the file holidays/name.txt in the config directory, or the built-in rules \"us\" (federal, observed) and \"si\" (Slovenia) for the years around today", "\"us\" holidays" },
  { "yholidays", 2, op_yholidays, "Workdays", "year name -- H", "the built-in rules for one year", "2027 \"si\" yholidays" },
  { "sethol", 1, op_sethol, "Workdays", "H --", "install a vector of dates YYYYMMDD as the holiday calendar for this session", "[2 1 $ 20261126 20261225] sethol" },
  { "usehol", 1, op_usehol, "Workdays", "name --", "install the calendar `name` (file or rules) and remember it in config.txt", "\"us\" usehol" },
  { "clrhol", 0, op_clrhol, "Workdays", "--", "remove the holiday calendar (weekends only) and forget it in config.txt", "clrhol" },
  { "hol", 0, op_hol, "Workdays", "-- H", "the installed holiday calendar as a vector", "hol" },
};

const Module mm41_mod_dates = {
  "dates", "1.1", "calendar arithmetic, workdays and holidays, time zones, sunrise and sunset",
  table, (int)(sizeof table / sizeof table[0]), NULL, workdays_shutdown, 0
};
