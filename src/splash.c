#ifndef BUILD_STAMP
#define BUILD_STAMP __DATE__ " " __TIME__
#endif

#include <stdio.h>        // for printf
#include <time.h>         // for ctime, time, time_t
#include "globals.h"      // for MM41_VERSION
#include "splash.h"       // for splash_screen

void splash_screen(void) {
  time_t now = time(NULL);
  char* started = ctime(&now);
  
  printf("\n");
  printf("╔══════════════════════════════════════════════╗\n");
  printf("║                                              ║\n");
  printf("║     Mico's Matrix & Scalar RPN Calculator    ║\n");
  {                                   /* centred whatever the length of the version */
    char v[48];
    int n = snprintf(v, sizeof v, "Version %s (2026)", MM41_VERSION);
    int left = (46 - n) / 2, right = 46 - n - left;
    printf("║%*s%s%*s║\n", left, "", v, right, "");
  }
  printf("║                                              ║\n");
  printf("║  > Enter RPN expressions                     ║\n");
  printf("║  > Type 'help' for commands                  ║\n");
  printf("║  > Press 'q' or ctrl+d to quit               ║\n");
  printf("║                                              ║\n");
  printf("╚══════════════════════════════════════════════╝\n");

  printf("mm41 git:     %s\n", VERSION);
  printf("Built:         %s\n", BUILD_STAMP);
  printf("Started:       %s", started);   // ctime() includes newline
  printf("\n");
}

