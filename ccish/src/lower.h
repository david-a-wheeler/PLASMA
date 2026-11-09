// ccgen's stage 2 (lower.c): IR ops to machine ops, and -i. Include
// after mop.h.
#ifndef LOWER_H
#define LOWER_H

void lower_op(int op, int a, int f);    // stage 2's entry
void mop_const(Mop *m, int v);         // make m push v
int  mop_const_value(Mop *m, int *v);   // does m push a constant?
void dump_ir(int op, int a, int f);    // one op, for -i
void out_num(const char *s, int n);    // s, then n, to stdout

// How lower_op() lowers each IR op (lower.c's L_*), indexed by op
// (IR_NOPS entries; no size here, so peep.c needn't include ir.h).
extern const char lw_kind[];
// Is op, a number below IR_NOPS, one some IR op has? lw_kind[] says
// (L_NONE, 0, for an UNUSED number in ir.h), so that it's right however
// ir.h numbers the ops and spaces their blocks.
#define IR_IS_OP(op)  (lw_kind[op] != 0)

#endif
