#ifndef HELP_H
#define HELP_H

#include "stack.h"


void help_menu(void);
void list_all_functions_sorted(void);
void usage(const char *name);
void help_topic(const char *topic);   /* "help npv", "help finance", "help holiday" */
int op_usage(Stack *stack);
void whose_place(void);

#endif //HELPER_FUNCTIONS_H
