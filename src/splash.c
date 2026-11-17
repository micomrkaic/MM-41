/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* The splash: the name as a dot-matrix wordmark in an LCD frame, the
   version on the annunciator row, and the build and start times below. */

#ifndef BUILD_STAMP
#define BUILD_STAMP __DATE__ " " __TIME__
#endif

#include <stdio.h>        // for printf
#include <string.h>
#include <time.h>         // for localtime, strftime, time
#include "globals.h"      // for MM41_VERSION
#include "skins.h"        // for skin_pixel_text
#include "splash.h"       // for splash_screen

#define INNER 46

void splash_screen(void) {
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  char started[64], year[8], ann[INNER + 1];
  strftime(started, sizeof started, "%a %d %b %Y %H:%M", lt);
  strftime(year, sizeof year, "%Y", lt);

  /* the wordmark: 5 characters, 6 pixels each, magnified twice = 60 pixels = 30 cells */
  const char *mark = (lt->tm_mon == 0 && lt->tm_mday == 1) ? "MEMORY LOST" : "MM-41";
  int cells = (int)strlen(mark) * 6;                      /* at scale 2: 2 pixels a cell, 12 pixels a character */
  int left = (INNER - cells) / 2, right = INNER - cells - left;
  char pre[32], post[32];
  snprintf(pre, sizeof pre, "   │%*s", left, "");
  snprintf(post, sizeof post, "%*s│", right, "");

  printf("\n");
  printf("   ┌──────────────────────────────────────────────┐\n");
  printf("   │%*s│\n", INNER, "");
  skin_pixel_text(mark, (int)strlen(mark), 2, pre, post);
  snprintf(ann, sizeof ann, " USER  RAD%*s", INNER - 10 - 1, "");
  {
    char tail[32]; snprintf(tail, sizeof tail, "%s  %s ", MM41_VERSION, year);
    size_t tl = strlen(tail);
    memcpy(ann + INNER - tl, tail, tl);
  }
  printf("   │%s│\n", ann);
  printf("   └──────────────────────────────────────────────┘\n");
  printf("     Mico's Matrix & Scalar RPN Calculator\n");
  printf("     help for the commands, help <name> for any one, q to quit\n");
  printf("\n");
  printf("     git %s · built %s · started %s\n", VERSION, BUILD_STAMP, started);
  printf("\n");
}
