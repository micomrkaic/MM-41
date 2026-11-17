/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

/* Nothing in this file is documented, listed, completed or described by
   help, and none of it touches the stack. A line is only taken here when
   every word on it is otherwise nothing in MM-41: a command or a user word
   of the same name is left alone. */

#include "eggs.h"
#include "ops.h"
#include "skins.h"
#include "words.h"
#include <stdio.h>
#include <string.h>

/* ---- one-liners ---- */

static int say(const char *what) { printf("%s\n", what); return 1; }

/* ---- the machine itself ---- */

static int hp41(void) {
  static const char *const art[] = {
    "        ╔═══════════════════════════════════╗",
    "        ║   ON     USER     PRGM    ALPHA   ║",
    "        ║  ┌──────────────────────────────┐ ║",
    "        ║  │                              │ ║",
    "        ║  │  0.0000                      │ ║",
    "        ║  │  USER      RAD               │ ║",
    "        ║  └──────────────────────────────┘ ║",
    "        ║                                   ║",
    "        ║   ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐   ║",
    "        ║   │Σ+ │ │1/x│ │√x │ │LOG│ │LN │   ║",
    "        ║   └───┘ └───┘ └───┘ └───┘ └───┘   ║",
    "        ║   ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐   ║",
    "        ║   │x≷y│ │R↓ │ │SIN│ │COS│ │TAN│   ║",
    "        ║   └───┘ └───┘ └───┘ └───┘ └───┘   ║",
    "        ║   ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐   ║",
    "        ║   │ ■ │ │XEQ│ │STO│ │RCL│ │SST│   ║",
    "        ║   └───┘ └───┘ └───┘ └───┘ └───┘   ║",
    "        ║   ┌─────────┐ ┌───┐ ┌───┐ ┌───┐   ║",
    "        ║   │ ENTER↑  │ │CHS│ │EEX│ │ ← │   ║",
    "        ║   └─────────┘ └───┘ └───┘ └───┘   ║",
    "        ║   ┌───┐   ┌────┐ ┌────┐ ┌────┐    ║",
    "        ║   │ − │   │ 7  │ │ 8  │ │ 9  │    ║",
    "        ║   └───┘   └────┘ └────┘ └────┘    ║",
    "        ║   ┌───┐   ┌────┐ ┌────┐ ┌────┐    ║",
    "        ║   │ + │   │ 4  │ │ 5  │ │ 6  │    ║",
    "        ║   └───┘   └────┘ └────┘ └────┘    ║",
    "        ║   ┌───┐   ┌────┐ ┌────┐ ┌────┐    ║",
    "        ║   │ × │   │ 1  │ │ 2  │ │ 3  │    ║",
    "        ║   └───┘   └────┘ └────┘ └────┘    ║",
    "        ║   ┌───┐   ┌────┐ ┌────┐ ┌────┐    ║",
    "        ║   │ ÷ │   │ 0  │ │ .  │ │R/S │    ║",
    "        ║   └───┘   └────┘ └────┘ └────┘    ║",
    "        ║                                   ║",
    "        ║            mm-41                  ║",
    "        ╚═══════════════════════════════════╝",
  };
  putchar('\n');
  for (size_t i = 0; i < sizeof art / sizeof art[0]; i++) puts(art[i]);
  putchar('\n');
  return 1;
}

/* ---- the code ---- */

static int code_state = 0;

/* What an HP-41 said when the batteries had been out too long. The skin
   advances to the next one. */
static int memory_lost(void) {
  skin = skin == SKIN_PLAIN ? SKIN_HP41 : (skin + 1) % SKIN_COUNT;
  putchar('\n'); skin_frame41("MEMORY LOST", NULL); putchar('\n');
  return 1;
}

static int konami(char *const *toks, int ntok) {
  static const char *const code[] = { "up", "up", "down", "down", "left", "right", "left", "right", "b", "a" };
  enum { CODE_LEN = sizeof code / sizeof code[0] };
  for (int i = 0; i < ntok; i++) {
    int known = 0;
    for (size_t k = 0; k < CODE_LEN; k++) if (strcmp(toks[i], code[k]) == 0) known = 1;
    if (!known) { code_state = 0; return 0; }
  }
  for (int i = 0; i < ntok; i++) {
    if (strcmp(toks[i], code[code_state]) == 0) code_state++;
    else code_state = strcmp(toks[i], code[0]) == 0 ? 1 : 0;
    if (code_state == CODE_LEN) { code_state = 0; memory_lost(); }
  }
  return 1;
}

/* ---- the gate ---- */

int egg_line(const char *line) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s", line);
  int ntok = 0;
  char *toks[64];
  for (char *t = strtok(buf, " \t"); t && ntok < 64; t = strtok(NULL, " \t")) toks[ntok++] = t;
  if (ntok == 0) return 0;
  for (int i = 0; i < ntok; i++)
    if (find_op(toks[i]) >= 0 || find_word(toks[i])) { code_state = 0; return 0; }   /* theirs, not ours */
  if (ntok == 1) {
    const char *w = toks[0];
    if (!strcmp(w, "fuck"))  return say("Your place or mine?");
    if (!strcmp(w, "xyzzy")) return say("Nothing happens.");
    if (!strcmp(w, "plugh")) return say("A hollow voice says \"Fool.\"");
    if (!strcmp(w, "forth")) return say("ok");
    if (!strcmp(w, "hvala")) return say("Ni za kaj.");
    if (!strcmp(w, "hp41") || !strcmp(w, "hp-41") || !strcmp(w, "41c")) return hp41();
  }
  return konami(toks, ntok);
}
