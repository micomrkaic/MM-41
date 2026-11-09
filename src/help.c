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
  printf("    erf erfc gammap gammaq betainc digamma zeta besselj bessely besseli besselk airyai airybi ellk elle expint lambertw\n");
  printf("    Real arguments outside the real domain promote to complex: -1 sqrt is (0,1).\n");
  subtitle("Decimals (34 digits, half-even; decimal absorbs binary: 0.1d 0.2 + is 0.3)");
  printf("    decimal binary (number mode)  >dec >bin  x n round  isdec decmode\n");
  subtitle("Finance (all decimal; rates in percent per period; cash received +)");
  printf("    n i%% pv pmt fv  ?n ?i%% ?pv ?pmt ?fv  beg end clrtvm tvm  amort amortm   cf rate npv  cf irr\n");
  printf("    nom>eff eff>nom  sl soyd db  bprice byield   xnpv xirr xprice xyield xaccr yearfrac  date>ymd ymd>date\n");
  subtitle("Statistics, random draws, differential equations (vectors are N x 1 or 1 x N matrices)");
  printf("    v mean sd sem median skew kurt  v p quantile  x y cov corr  v n movavg  v k hist   x y linfit expfit logfit powfit\n");
  printf("    X y ols olstab   tcdf tinv chi2cdf chi2inv fcdf finv binompdf binomcdf poispdf poiscdf\n");
  printf("    n seed  rnorm runif rpois rexp  n ... normrnd unifrnd poisrnd binomrnd exprnd trnd chi2rnd  resample shuffle  n @f sims  mcstats\n");
  printf("    y0 t0 t1 n @f odeint   y0 t0 t1 @f odesol      (the word is  t y -- y')\n");
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
  printf("             (at the prompt: use name, unuse name, load file.so, usehol name -- TAB completes the name)\n");
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
    printf("    help modules        what modules are and how to add one; also help words, decimal, finance,\n");
    printf("                        workdays, numerics, stat, ode, syntax\n");
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
  { "v2",       "a vector: an N x 1 or 1 x N real matrix" },
  { "X",        "a real matrix, one observation per row" },
  { "y0",       "the initial state: a real, or a row vector for a system" },
  { "t0",       "a real number, the start" },
  { "t1",       "a real number, the end" },
  { "df",       "degrees of freedom (positive)" },
  { "df1",      "degrees of freedom (positive)" },
  { "df2",      "degrees of freedom (positive)" },
  { "mu",       "a real number, the mean" },
  { "sigma",    "a real number >= 0, the standard deviation" },
  { "coeffs",   "polynomial coefficients as a row vector, highest power first" },
  { "[a,b,r2]", "a row vector: the two coefficients and R-squared" },
  { "[mean,sd,q05,q50,q95]", "a row vector: mean, standard deviation, 5th, 50th and 95th percentiles" },
  { "beta",     "a column vector of coefficients, the intercept first" },
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
  { "cf",       "cash flows: a column vector in time order, the first at t = 0, or a matrix with two columns, amount and repeat count" },
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
  if (!strncmp(op->example, ": ", 2)) return false;  /* would define a word in the session */
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
    bool vectors = m && (!strcmp(m->name, "stat") || !strcmp(m->name, "random"));   /* x, y are data vectors there */
    for (int i = 0; i < nl; i++) {
      const char *lg = legend_for(left[i]);
      if (vectors && (!strcmp(left[i], "x") || !strcmp(left[i], "y"))) lg = "a data vector: an N x 1 or 1 x N real matrix";
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


/* ---- concept pages: what a thing is and how it is used, before the listings ---- */
typedef struct { const char *name; const char *text; } Topic;
static const Topic topics[] = {
  { "modules",
    "A module is a group of commands that plugs into the calculator, like a ROM in an HP-41.\n"
    "Every command belongs to exactly one module (help <name> says which), no two modules may\n"
    "define the same name, and from the stack all modules look the same. There are three kinds:\n"
    "\n"
    "  built in      compiled into mm41: core, decimal, finance, complex, special, matrices,\n"
    "                numerics, strings, registers, dates, programs. `make MODULES=\"...\"` builds a\n"
    "                calculator with a subset; core is always there.\n"
    "  word modules  a text file of word definitions, NAME.mm, in ~/.config/mm41/modules/:\n"
    "                first line  \\ module NAME VERSION SUMMARY\n"
    "                then        name body         or   : name body ;   one word per line\n"
    "                Every file there loads at startup; \"name\" use loads one now (by name,\n"
    "                or a path), \"name\" unuse removes it. At the prompt, use name and\n"
    "                unuse name alone on a line mean the same (TAB completes the name).\n"
    "                std.mm and const.mm ship with mm41.\n"
    "  shared objects   a compiled module, NAME.so, written in C against include/module.h:\n"
    "                `make shared-module DIR=path` builds it, \"path/NAME.so\" load loads it,\n"
    "                and ~/.config/mm41/modules/*.so load at startup. examples/modules/hello\n"
    "                is a complete one (two commands, 40 lines).\n"
    "\n"
    "  modules            what is loaded, with versions and word counts\n"
    "  help <module>      every command of one module, e.g. help finance\n"
    "  listfcns           everything, by module and category\n"
    "Manual: chapter Modules.\n" },
  { "words",
    "A word is a command you define: : name body ;  e.g.  : sq dup * ;  then  5 sq  gives 25.\n"
    "Words run like built-in commands, can call other words and themselves, and are what the\n"
    "numerical methods take as functions (@sq pushes a token for it). Inside a word:\n"
    "  flag if ... else ... then      begin ... flag until      begin ... flag while ... repeat\n"
    "  limit start do ... loop        i (loop index)  j        n +loop\n"
    "listwords shows yours, \"name\" delword deletes one, clrwords all; savewords writes them\n"
    "to ~/.config/mm41/user_words.txt, which loads at startup. A word is one line; put a\n"
    "family of them in a word module instead (help modules). Manual: chapter Words.\n" },
  { "decimal",
    "A decimal is an exact 34-digit number for money (IBM decNumber, round half-even), the\n"
    "fifth stack type, shown with the glyph 𝔻. 0.1d 0.2d + is exactly 0.3; 0.1 0.2 + is not.\n"
    "  0.1d          a decimal literal;  2b forces a binary double\n"
    "  decimal       number mode: every literal is a decimal until `binary` (prompt MM-41[dec]>>)\n"
    "  >dec >bin     convert; x n round rounds half-even to n places\n"
    "Decimal absorbs binary: a decimal combined with a real gives a decimal. Arithmetic,\n"
    "percentages, comparisons, registers and the finance words take decimals; trigonometry,\n"
    "complex numbers, matrices and the numerical methods are binary and ask for >bin. A decimal\n"
    "used as a count, index, precision or flag is converted silently. Manual: chapter Decimal numbers.\n" },
  { "finance",
    "The finance module is MM-B1 rebuilt as a module. Three conventions throughout:\n"
    "  every word computes in decimals and leaves a decimal (2 setprec shows cents);\n"
    "  rates are in percent per period (0.5 i% is 6% a year paid monthly); the dated words\n"
    "  (xnpv xirr xprice xyield) take annual rates;\n"
    "  cash received is positive, cash paid is negative.\n"
    "Time value of money: store with n i% pv pmt fv, solve with ?n ?i% ?pv ?pmt ?fv, beg/end\n"
    "for the payment timing, tvm shows the registers, clrtvm clears them:\n"
    "    clrtvm 360 n 0.5 i% 300000 pv 0 fv ?pmt      -> -1798.65\n"
    "Cash flows are vectors in time order, first at t = 0:  [5 1 $ -5000 1500 1500 1500 1500] irr\n"
    "Dates are \"d.m.yyyy\" strings or YYYYMMDD numbers. Manual: chapter Finance.\n" },
  { "workdays",
    "A workday is a Monday to Friday not in the holiday calendar. Excel's conventions:\n"
    "  d1 d2 workdays        both ends inclusive (NETWORKDAYS), negative if d2 < d1\n"
    "  date n workdayplus    n workdays later, start day not counted (WORKDAY)\n"
    "  date isworkday\n"
    "The calendar is empty (weekends only) until you install one:\n"
    "  \"us\" usehol           the US federal holidays (observed), remembered in config.txt (or: usehol us)\n"
    "  \"si\" usehol           Slovenia\n"
    "  \"name\" usehol         ~/.config/mm41/holidays/name.txt, one date per line (example.txt shows the format)\n"
    "hol shows it, clrhol removes it, \"us\" holidays and 2027 \"si\" yholidays push one as a vector.\n"
    "Manual: section Workdays and holidays.\n" },
  { "stat",
    "Statistics work on vectors, real matrices with one row or one column, and leave reals:\n"
    "  v mean sd sem median skew kurt      v p quantile      x y cov      x y corr\n"
    "  v n movavg      v k hist (bin centre, count)\n"
    "Fits leave a row vector [a b r2]:  x y linfit (y=a+bx)  expfit (a e^bx)  logfit (a+b ln x)  powfit (a x^b)\n"
    "  X y ols -- beta        multiple regression on the columns of X, intercept added\n"
    "  X y olstab -- M r2     coefficient, standard error, t, p per row, and R^2\n"
    "Distributions: x df tcdf, p df tinv, chi2cdf chi2inv, x df1 df2 fcdf, finv, k n p binompdf/binomcdf,\n"
    "k mu poispdf/poiscdf; the normal is ncdf / nquant.\n"
    "Random (module random): n seed; one draw mu sigma rnorm, a b runif, mu rpois, rate rexp;\n"
    "n draws as a column n mu sigma normrnd, n a b unifrnd, n mu poisrnd, n m p binomrnd, n rate exprnd,\n"
    "n df trnd, n df chi2rnd; v resample (bootstrap), v shuffle; n @f sims runs a word n times and\n"
    "collects what it leaves; v mcstats -- [mean sd q05 q50 q95].  Manual: chapter Statistics and simulation.\n" },
  { "ode",
    "y' = f(t, y) with an adaptive Runge-Kutta-Fehlberg (4,5) driver, to the tolerance tol.\n"
    "The word takes t and y and leaves y':   : f swap drop chs ;   is y' = -y.\n"
    "  y0 t0 t1 n @f odeint -- M    the trajectory: n+1 rows of t and the state\n"
    "  y0 t0 t1 @f odesol -- y      the state at t1\n"
    "A system is the same with y a row vector: the word gets t and a 1 x k matrix and leaves a vector of k\n"
    "(1 1 ones * makes a 1 x 1 matrix from a real; join_h assembles the row). Manual: chapter Differential equations.\n" },
  { "numerics",
    "The numerical methods take a function as an execution token: @name, for a word or a command.\n"
    "  a b @f integrate     a b @f romberg      a b @f fzero       x @f solve\n"
    "  a b @f minimize      x @f deriv          lo hi @f fsum      lo hi @f fprod     @f exec\n"
    "The word takes x from the stack and leaves f(x):   : f dup * 2 - ;   0 3 @f fzero  -> 1.414214\n"
    "x tol sets the tolerance (kept in config.txt); nevals and interr report the last call.\n"
    "Manual: chapter Numerical methods.\n" },
  { "syntax",
    "Tokens are separated by spaces; numbers go on the stack, commands take their arguments\n"
    "from it (3 4 + is 7). Numbers 3 -1.2e-4 .5, decimals 0.1d, complex (1,2), strings \"text\",\n"
    "matrices [rows cols $ a11 a12 ...] or [rows, cols, \"file\"]. Comments start with \\ or #.\n"
    "The stack prints after every line, [0] at the bottom; undo restores the previous stack,\n"
    "q quits. help <name> describes any command.\n"
    "Commands that take a name take a string: \"const\" use, \"us\" usehol, \"f.so\" load. A line\n"
    "that is only such a command (use unuse load run batch usehol) and one bare name means\n"
    "the same: use const, usehol us. Anything longer is evaluated as written.\n"
    "Manual: chapter Entering data.\n" },
};

static int describe_topic(const char *name) {
  for (size_t i = 0; i < sizeof topics / sizeof topics[0]; i++) {
    if (strcasecmp(topics[i].name, name) != 0) continue;
    printf("\n" BOLD "%s" RESET "\n%s\n", topics[i].name, topics[i].text);
    return 1;
  }
  return 0;
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
    printf("    help <topic>         a short introduction: modules, words, decimal, finance, workdays, numerics, stat, ode, syntax\n");
    printf("    help <command>       one command: stack entries, result, example (run when harmless)\n");
    printf("    help <module>        every command of a module, e.g. help finance\n");
    printf("    help <category>      every command of a category, e.g. help Stack\n");
    printf("    help <word>          a user word's definition\n");
    printf("    help <text>          commands whose name or description mentions the text\n");
    printf("    listfcns  modules    the complete listings\n\n");
    return;
  }
  if (describe_topic(t)) {                      /* the concept first, then the listing of the same name */
    if (!strcmp(t, "modules")) list_modules();
    else describe_module(t);
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
