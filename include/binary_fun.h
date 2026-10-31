/*
 * This file is part of MM-41
 *
 * MM-41 is free software: 
 * you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MM-41 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MM-41. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef BINARY_FUN_H
#define BINARY_FUN_H

void add_top_two_scalars(Stack* stack);
void add_top_two_matrices(Stack* stack);
void multiply_top_two_scalars(Stack* stack);
void subtract_top_two_scalars(Stack* stack);
void divide_top_two_scalars(Stack* stack);
void subtract_top_two_matrices(Stack* stack);
void multiply_top_two_matrices(Stack* stack);
void add_top_two(Stack* stack);
void sub_top_two(Stack* stack);
void mul_top_two(Stack* stack);
void div_top_two(Stack* stack);
void pow_top_two(Stack* stack);
void join_2_reals(Stack *s);
int kronecker_top_two(Stack* stack);
void dot_div_top_two(Stack* stack);
void dot_mult_top_two(Stack* stack);
void dot_pow_top_two(Stack* stack);

#endif //BINARY_FUN_H

  
