#define _POSIX_C_SOURCE 200112L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include "errors.h"
#include <stdbool.h>        // for true
#include <stdio.h>          // for printf, fprintf, NULL, stderr
#include <stdlib.h>         // for free, malloc, qsort
#include <string.h>         // for strcmp
#include "ops.h"            // for ops, find_op
#include "module.h"         // for module_of
#include "words.h"          // for find_word
#include "globals.h"        // for skip_stack_printing
#include "help.h"           // for HelpEntry, help_menu, list_all_functions
#include "stack.h"          // for (anonymous struct)::(anonymous), TYPE_STRING
#include "eval_fun.h"       // for evaluate_line (running examples)
#include "print_fun.h"      // for print_stack
#include <strings.h>        // for strcasecmp

static void describe_op(int idx);

#define BOLD      "\033[1m"
#define ITALIC    "\033[3m"
#define UNDERLINE "\033[4m"
#define RESET     "\033[0m"

#define title(s) printf(BOLD s RESET "\n")
#define subtitle(s) printf(UNDERLINE s RESET "\n")

void whose_place(void) {
  printf("Your place or mine?\n");
  return;
}

void help_menu(void) {
  printf("\n");
  title("MM-41 " MM41_VERSION " -- RPN calculator for real, complex and decimal scalars, strings and matrices");
  subtitle("Entering data");
  printf("    Numbers 3  -1.2e-4   decimal 0.1d   complex (1,2)   strings \"text\"   all input is case sensitive\n");
  printf("    Matrices [rows cols $ a11 a12 ...]  e.g. [2 2 $ 1 2 3 4];  from a file [2, 2, \"m.txt\"]\n");
  printf("    Comments start with \\ or #.  undo restores the stack before the previous line.\n");
  subtitle("Stack");
  printf("    dup drop swap over nip tuck  n pick  n roll  depth  clst  ps  print  pm\n");
  subtitle("Arithmetic and functions");
  printf("    + - * / ^  chs inv abs sqrt mod fact ncr npr  %% %%t %%chg   .* ./ .^ (elementwise)\n");
  printf("    exp ln log  sin cos tan asin acos atan  sinh cosh tanh asinh acosh atanh\n");
  printf("    re im arg conj re2c split_c j2r   npdf ncdf nquant gamma ln_gamma beta ln_beta\n");
  printf("    Real arguments outside the real domain promote to complex: -1 sqrt is (0,1).\n");
  subtitle("Decimals (34 digits, half-even; decimal absorbs binary: 0.1d 0.2 + is 0.3)");
  printf("    decimal binary (number mode)  >dec >bin  x n round  isdec decmode\n");
  subtitle("Finance (all decimal; rates in percent per period; cash received +)");
  printf("    n i%% pv pmt fv  ?n ?i%% ?pv ?pmt ?fv  beg end clrtvm tvm  amort amortm   cf rate npv  cf irr\n");
  printf("    nom>eff eff>nom  sl soyd db  bprice byield   xnpv xirr xprice xyield xaccr yearfrac  date>ymd ymd>date\n");
  subtitle("Matrices");
  printf("    tran ' minv pinv det eig chol svd kron  eye ones zeroes rand randn rrange\n");
  printf("    dim reshape get_aij set_aij split_mat diag to_diag join_v join_h\n");
  printf("    csum rsum cmean rmean cvar rvar cmin rmin cmax rmax cumsum_r cumsum_c  roots pval\n");
  subtitle("Words (user-defined commands)");
  printf("    : name body ;     e.g.  : sq dup * ;     then  5 sq\n");
  printf("    inside a word: if else then   begin until   begin while repeat   do loop +loop  i j\n");
  printf("    listwords  \"name\" usage  \"name\" delword  savewords  listmacros\n");
  subtitle("Modules");
  printf("    modules  \"name\" use  \"name\" unuse  \"file.so\" load     word modules: ~/.config/mm41/modules/*.mm\n");
  subtitle("Numerical methods (@name pushes a function token)");
  printf("    a b @f integrate   a b @f romberg   a b @f fzero   x @f solve   a b @f minimize\n");
  printf("    x @f deriv   lo hi @f fsum   lo hi @f fprod   @f exec   x tol   nevals  interr\n");
  subtitle("Registers, dates, strings, programs");
  printf("    x n sto  n rcl  pr  ffr  saveregs loadregs      today now dow dateplus ddays sunrise ...\n");
  printf("    d1 d2 workdays   date n workdayplus   \"us\" usehol  (holiday calendars: us, si, or holidays/*.txt)\n");
  printf("    scon substr s2u s2l slen srev int2str eval      \"file\" run   \"file\" batch   !shell command\n");
  subtitle("More help");
  printf("    listfcns            every command by module and category, with stack pictures\n");
  printf("    modules             the loaded modules\n");
    printf("    help <name>         one command in full: what it takes, what it leaves, its example run;\n");
    printf("                        also help <module>, help <category>, help <text> (search), help help\n");
  printf("    man mm41, and the manual mm41-manual.pdf in the share/doc directory\n");
  printf("\n");
  skip_stack_printing = true;
}

void list_all_functions_sorted(void) {
  list_ops_by_category();
}

/* Public function: print "name" usage: ... */
void usage(const char *name) {
    if (!name || !*name) {
        mm_error("usage: expected non-empty word name\n");
        skip_stack_printing = true;
        return;
    }

    int idx = find_op(name);
    if (idx < 0) {
        user_word *w = find_word(name);
        if (w) { printf(": %s %s ;\n", w->name, w->body); skip_stack_printing = true; return; }
        mm_error("No usage information available for \"%s\".\n", name);
        skip_stack_printing = true;
        return;
    }
    describe_op(idx);
    skip_stack_printing = true;
}

/*
 * RPN word: usage
 *
 * Stack effect (logical):  name -- 
 * where `name` is a TYPE_STRING containing the word name, e.g. "sin".
 *
 * Behavior:
 *   - Pops a string from the stack
 *   - Calls usage(name) to print `"name" usage: ...`
 *   - Frees the popped string
 *   - On error (wrong type / empty string), prints a message,
 *     restores the stack element, and returns 0.
 *
 * Return value:
 *   1 on success, 0 on error.
 */

int op_usage(Stack *stack)
{
    /* Need at least one item on the stack */
    GUARANTEE_STACK(stack, 1);

    stack_element e = pop(stack);

    if (e.type != TYPE_STRING) {
        /* Wrong type: restore element and complain */
        mm_error("usage: expected a string with a word name on the stack.\n");

        /* Push it back based on its type so we don't silently lose data */
        switch (e.type) {
        case TYPE_REAL:
            push_real(stack, e.real);
            break;
        case TYPE_COMPLEX:
            push_complex(stack, e.complex_val);
            break;
        case TYPE_STRING:
            /* not possible here, but keep for completeness */
            push_string(stack, e.string);
            break;
        case TYPE_MATRIX_REAL:
            push_matrix_real(stack, e.matrix_real);
            break;
        case TYPE_MATRIX_COMPLEX:
            push_matrix_complex(stack, e.matrix_complex);
            break;
        default:
            /* Unknown type: nothing we can safely do */
            break;
        }

        return 0;
    }

    /* We have a string with the word name */
    const char *name = e.string ? e.string : "";

    if (name[0] == '\0') {
        mm_error("usage: empty string is not a valid word name.\n");
        free(e.string);
        return 0;
    }

    /* Delegate to the help system */
    usage(name);

    /* The usage() function already sets skip_stack_printing,
       but it's harmless to enforce it here too if you want: */
    skip_stack_printing = true;

    /* We own the string memory, so free it after use */
    free(e.string);

    return 1;
}

/* ======================================================================
 * help <topic>: one command in full, a module, a category, a word, or a
 * search of names and descriptions.
 * ====================================================================== */

/* What the operand names in the stack pictures stand for. The pictures are
   terse by design ("cf rate -- x"); this table turns them into the "needed
   stack entries" line by line. Matching is exact on the token, after the
   outer quotes are stripped. */
typedef struct { const char *token; const char *meaning; } Legend;
static const Legend legend[] = {
  { "x",        "a number: real, or complex, decimal or matrix where allowed" },
  { "y",        "a number: real, or complex, decimal or matrix where allowed" },
  { "z",        "a real or complex number (a matrix is taken elementwise)" },
  { "a",        "a real number" },
  { "b",        "a real number" },
  { "n",        "a whole number (real or decimal)" },
  { "k",        "a whole number" },
  { "m",        "a whole number" },
  { "i",        "a whole number" },
  { "j",        "a whole number" },
  { "p",        "a probability in [0, 1]" },
  { "rows",     "a whole number of rows" },
  { "cols",     "a whole number of columns" },
  { "step",     "a real number" },
  { "start",    "a real number" },
  { "end",      "a real number" },
  { "lo",       "a real number, the lower bound" },
  { "hi",       "a real number, the upper bound" },
  { "guess",    "a real number near the root" },
  { "flag",     "0 or 1 (any nonzero counts as true)" },
  { "A",        "a matrix (real or complex)" },
  { "B",        "a matrix (real or complex)" },
  { "M",        "a real matrix" },
  { "I",        "a matrix" },
  { "V",        "a matrix" },
  { "v",        "a vector: an N x 1 or 1 x N real matrix" },
  { "coeffs",   "polynomial coefficients as a row vector, highest power first" },
  { "s",        "a string" },
  { "s1",       "a string" },
  { "s2",       "a string" },
  { "sub",      "a string" },
  { "name",     "a string with the name" },
  { "file.so",  "a string with the path of a shared object" },
  { "xt",       "an execution token: @word or @command (see the numerics module)" },
  { "date",     "a date string \"d.m.yyyy\"; the finance and workday words also take the number YYYYMMDD" },
  { "date2",    "a date string \"d.m.yyyy\"; the finance and workday words also take the number YYYYMMDD" },
  { "d1",       "a date: \"d.m.yyyy\" or YYYYMMDD" },
  { "d2",       "a date: \"d.m.yyyy\" or YYYYMMDD" },
  { "settle",   "the settlement date: \"d.m.yyyy\" or YYYYMMDD" },
  { "maturity", "the maturity date: \"d.m.yyyy\" or YYYYMMDD" },
  { "yyyymmdd", "a date as the whole number YYYYMMDD" },
  { "year",     "a whole number" },
  { "time",     "a time string \"HH:MM\"" },
  { "hours",    "a real number of hours" },
  { "zone",     "an IANA time zone name such as \"Europe/Ljubljana\", or a UTC offset in hours" },
  { "lat",      "latitude in decimal degrees, north positive" },
  { "lon",      "longitude in decimal degrees, east positive" },
  { "H",        "a vector of dates YYYYMMDD" },
  { "cf",       "cash flows: a real vector in time order, the first at t = 0, or an N x 2 matrix of amount and repeat count" },
  { "rate",     "an interest rate in percent (per period; per year for the dated words)" },
  { "yld",      "a yield in percent" },
  { "cpn",      "a coupon rate in percent" },
  { "nom",      "a nominal annual rate in percent" },
  { "eff",      "an effective annual rate in percent" },
  { "pv",       "an amount; cash received is positive, cash paid negative" },
  { "pmt",      "an amount; cash received is positive, cash paid negative" },
  { "fv",       "an amount; cash received is positive, cash paid negative" },
  { "price",    "a price per 100 of face value" },
  { "redeem",   "the redemption value per 100 of face value" },
  { "cost",     "an amount" },
  { "salvage",  "an amount" },
  { "life",     "a number of periods" },
  { "factor",   "the declining-balance factor (2 = double declining)" },
  { "freq",     "coupons per year: 1, 2, 3, 4, 6 or 12" },
  { "basis",    "day count: 0 30/360, 1 act/act, 2 act/360, 3 act/365, 4 30E/360" },
  { "accrued",  "accrued interest per 100 of face value" },
  { "...",      "any further items" },
};

static const char *legend_for(const char *tok) {
  char t[64];
  snprintf(t, sizeof t, "%s", tok);
  size_t n = strlen(t);
  if (n >= 2 && t[0] == '"' && t[n - 1] == '"') { t[n - 1] = '\0'; memmove(t, t + 1, n - 1); }
  for (size_t i = 0; i < sizeof legend / sizeof legend[0]; i++)
    if (strcmp(legend[i].token, t) == 0) return legend[i].meaning;
  return NULL;
}

/* Split "a b -- c d" into its two sides; tokens are whitespace separated. */
static int side_tokens(const char *side, char out[][64], int max) {
  int n = 0;
  const char *p = side;
  while (*p && n < max) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *q = p;
    while (*q && *q != ' ') q++;
    size_t len = (size_t)(q - p);
    if (len >= 64) len = 63;
    memcpy(out[n], p, len); out[n][len] = '\0';
    n++;
    p = q;
  }
  return n;
}

/* Examples with side effects on the session are shown but not run. */
static bool example_is_safe(const Op *op, const Module *m) {
  static const char *cats[] = { "Session", "Registers", "Programs", "Words", "Modules", "Display" };
  static const char *names[] = { "n", "i%", "pv", "pmt", "fv", "?n", "?i%", "?pv", "?pmt", "?fv", "beg", "end",
                                 "clrtvm", "tvm", "amort", "amortm", "decimal", "binary", "setprec", "seed",
                                 "savestack", "loadstack", "eval", "exec", "run", "batch", "today", "now",
                                 "usehol", "sethol", "clrhol", "load", "use", "unuse" };
  for (size_t i = 0; i < sizeof cats / sizeof cats[0]; i++) if (strcmp(op->cat, cats[i]) == 0) return false;
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) if (strcmp(op->name, names[i]) == 0) return false;
  if (m && strcmp(m->name, "programs") == 0) return false;
  if (strchr(op->example, '@')) return false;        /* needs a word the user may not have */
  return true;
}

/* Run the example on a scratch stack and show what it leaves. */
static void run_example(const Op *op) {
  Stack scratch;
  init_stack(&scratch);
  char line[512];
  snprintf(line, sizeof line, "%s", op->example);
  long before = error_count;
  bool quiet_was = errors_quiet;
  errors_quiet = true;
  evaluate_line(&scratch, line);
  errors_quiet = quiet_was;
  if (error_count == before) {
    if (scratch.top < 0) printf("    which leaves an empty stack\n");
    else { printf("    which leaves:"); print_stack(&scratch, NULL); }
  }
  free_stack(&scratch);
}

static void describe_op(int idx) {
  const Op *op = &ops[idx];
  const Module *m = module_of(idx);
  printf("\n" BOLD "%s" RESET "   %s   [module %s, %s]\n", op->name, op->effect, m ? m->name : "?", op->cat);
  if (op->help[0]) printf("    %s\n", op->help);

  char left[16][64], right[16][64];
  const char *dash = strstr(op->effect, "--");
  char ls[256] = "", rs[256] = "";
  if (dash) {
    snprintf(ls, sizeof ls, "%.*s", (int)(dash - op->effect), op->effect);
    snprintf(rs, sizeof rs, "%s", dash + 2);
  }
  int nl = side_tokens(ls, left, 16), nr = side_tokens(rs, right, 16);

  if (nl == 0) printf("    takes nothing from the stack\n");
  else {
    printf("    takes %d item%s (the last one named is on top):\n", nl, nl == 1 ? "" : "s");
    for (int i = 0; i < nl; i++) {
      const char *lg = legend_for(left[i]);
      printf("      %-10s %s\n", left[i], lg ? lg : "");
    }
  }
  if (nr == 0) printf("    leaves nothing\n");
  else {
    printf("    leaves %d item%s:\n", nr, nr == 1 ? "" : "s");
    bool finance = m && strcmp(m->name, "finance") == 0;
    for (int i = 0; i < nr; i++) {
      const char *lg = legend_for(right[i]);
      if (finance && !strcmp(op->cat, "Finance") && strcmp(right[i], "M") != 0 && strcmp(right[i], "date") != 0 && strcmp(right[i], "yyyymmdd") != 0)
        lg = "a decimal (the finance words always compute in decimals)";
      printf("      %-10s %s\n", right[i], lg ? lg : "");
    }
  }
  if (op->example[0]) {
    printf("    example:  %s\n", op->example);
    if (example_is_safe(op, m)) run_example(op);
  }
  /* related: other commands of the same category in the same module */
  if (m) {
    int base = module_first_op(m), shown = 0;
    for (int i = base; i < base + m->n_ops; i++) {
      if (i == idx || strcmp(ops[i].cat, op->cat) != 0) continue;
      if (shown == 0) printf("    see also: ");
      if (shown > 0 && shown % 12 == 0) printf("\n              ");
      printf("%s ", ops[i].name);
      shown++;
    }
    if (shown) printf("\n");
  }
  printf("\n");
}

static void describe_word(const user_word *w) {
  printf("\n" BOLD "%s" RESET "   a %s%s%s\n", w->name,
         w->is_macro ? "word from the module " : "user word",
         w->is_macro ? w->module : "", w->is_macro ? "" : " (user_words.txt)");
  printf("    : %s %s ;\n", w->name, w->body);
  printf("    (words have no declared stack picture; read the body right to left for what it consumes)\n\n");
}

static int describe_module(const char *name) {
  for (int mi = 0; mi < modules_count(); mi++) {
    const Module *m = module_at(mi);
    if (strcmp(m->name, name) != 0) continue;
    printf("\n" BOLD "module %s %s" RESET "   %s\n", m->name, m->version, m->summary);
    int base = module_first_op(m);
    for (int i = base; i < base + m->n_ops; i++)
      printf("    %-12s %-34s %s\n", ops[i].name, ops[i].effect, ops[i].help);
    printf("    'help <name>' for any of them\n\n");
    return 1;
  }
  for (int i = 0; i < word_module_count; i++) {
    if (strcmp(word_modules[i].name, name) != 0) continue;
    printf("\n" BOLD "word module %s %s" RESET "   %s\n", word_modules[i].name, word_modules[i].version,
           word_modules[i].summary ? word_modules[i].summary : "");
    for (int k = 0; k < macro_count; k++)
      if (strcmp(macros[k].module, name) == 0) printf("    : %s %s ;\n", macros[k].name, macros[k].body);
    printf("\n");
    return 1;
  }
  return 0;
}

static int describe_category(const char *name) {
  int shown = 0;
  for (int i = 0; i < n_ops; i++) {
    if (strcasecmp(ops[i].cat, name) != 0) continue;
    if (!shown) printf("\n" BOLD "%s" RESET "\n", ops[i].cat);
    printf("    %-12s %-34s %s\n", ops[i].name, ops[i].effect, ops[i].help);
    shown++;
  }
  if (shown) printf("\n");
  return shown;
}

static const char *ci_strstr(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  if (n == 0) return hay;
  for (; *hay; hay++)
    if (strncasecmp(hay, needle, n) == 0) return hay;
  return NULL;
}

/* Names and descriptions containing the text, for "help holiday". */
static int apropos(const char *text) {
  int shown = 0;
  for (int i = 0; i < n_ops; i++) {
    if (!ci_strstr(ops[i].name, text) && !ci_strstr(ops[i].help, text)) continue;
    if (!shown) printf("\ncommands mentioning \"%s\":\n", text);
    printf("    %-12s %-34s %s\n", ops[i].name, ops[i].effect, ops[i].help);
    shown++;
  }
  for (int k = 0; k < word_count; k++)
    if (ci_strstr(words[k].name, text)) { printf("    : %s %s ;\n", words[k].name, words[k].body); shown++; }
  for (int k = 0; k < macro_count; k++)
    if (ci_strstr(macros[k].name, text)) { printf("    : %s %s ;   (%s)\n", macros[k].name, macros[k].body, macros[k].module); shown++; }
  if (shown) printf("\n");
  return shown;
}

void help_topic(const char *topic) {
  skip_stack_printing = true;
  while (*topic == ' ' || *topic == '\t') topic++;
  char t[128];
  snprintf(t, sizeof t, "%s", topic);
  size_t n = strlen(t);
  while (n > 0 && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\n')) t[--n] = '\0';
  if (n >= 2 && t[0] == '"' && t[n - 1] == '"') { t[n - 1] = '\0'; memmove(t, t + 1, n - 1); n -= 2; }
  if (n == 0) { help_menu(); return; }

  if (!strcmp(t, "help")) {
    printf("\n" BOLD "help" RESET "\n");
    printf("    help                 the overview\n");
    printf("    help <command>       one command: stack entries, result, example (run when harmless)\n");
    printf("    help <module>        every command of a module, e.g. help finance\n");
    printf("    help <category>      every command of a category, e.g. help Stack\n");
    printf("    help <word>          a user word's definition\n");
    printf("    help <text>          commands whose name or description mentions the text\n");
    printf("    listfcns  modules    the complete listings\n\n");
    return;
  }
  int idx = find_op(t);
  if (idx >= 0) { describe_op(idx); return; }
  user_word *w = find_word(t);
  if (w) { describe_word(w); return; }
  if (describe_module(t)) return;
  if (describe_category(t)) return;
  if (apropos(t)) return;
  printf("nothing called \"%s\": no command, word, module or category, and no description mentions it\n", t);
}
