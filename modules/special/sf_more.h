/*
 * This file is part of MM-41
 *
 * MM-41 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See <https://www.gnu.org/licenses/>.
 */

#ifndef MM41_SF_MORE_H
#define MM41_SF_MORE_H
#include "stack.h"
int op_erf(Stack *s);     int op_erfc(Stack *s);
int op_airyai(Stack *s);  int op_airybi(Stack *s);
int op_ellk(Stack *s);    int op_elle(Stack *s);
int op_zeta(Stack *s);    int op_digamma(Stack *s);
int op_expint(Stack *s);  int op_lambertw(Stack *s);
int op_besselj(Stack *s); int op_bessely(Stack *s); int op_besseli(Stack *s); int op_besselk(Stack *s);
int op_gammap(Stack *s);  int op_gammaq(Stack *s);  int op_betainc(Stack *s);
#endif
