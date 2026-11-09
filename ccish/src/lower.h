// ccgen's stage 2 (lower.c): IR ops to machine ops, and -i. Include
// after mop.h.
#ifndef LOWER_H
#define LOWER_H

void lower_op(int op, int a, int f);    // stage 2's entry
void mop_const(Mop *m, int v);         // make m push v
int  mop_const_value(Mop *m, int *v);   // does m push a constant?
void dump_ir(int op, int a, int f);    // one op, for -i
void out_num(const char *s, int n);    // s, then n, to stdout

#endif
