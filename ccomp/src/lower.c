/*
 * Stage 2: lowering, the first stage of the back end (ccgen). Turns IR
 * ops, as irin.c reads them from the IR file, into PLASMA machine ops
 * and chooses their encodings, mostly 1:1. Each IR op is described in
 * ../IR.md. -i prints each op (dump_ir()) as irin.c reads it.
 */
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "obj.h"
#include "ir.h"
#include "mop.h"
#include "irin.h"
#include "lower.h"
#include "peep.h"
#include "helper.h"

/* Each IR op's name, for -i. */
static const char *const ir_names[] = {
  "FUNC", "ENDFUNC", "CONST", "DATAADDR", "STRADDR", "LOCAL", "SETLOCAL",
  "LOCALB", "SETLOCALB", "LOCALADDR", "GLOBAL", "SETGLOBAL", "GLOBALB",
  "SETGLOBALB", "LOAD", "LOADB", "STORE", "STOREB", "CALL",
  "DROP", "DUP", "RETURN",
  "ADD", "SUB", "MUL", "DIV", "MOD", "AND", "OR", "XOR", "SHL", "SHR",
  "EQ", "NE", "LT", "GE", "GT", "LE",
  "ULT", "UGE", "UGT", "ULE",
  "NEG", "COMP", "NOT",
  "LABEL", "JUMP", "BRFALSE", "BRTRUE", "SEL",
  "JOIN", "CASES", "CASEVAL", "CASETO", "SWAP",
  "UDIV", "UMOD", "USHR", "HEAPADDR",
  "LOOP", "LOOPSTEP", "LOOPBODY", "LOOPEND"
};

/* How each IR op lowers (lw_kind[]), and the PLASMA opcode it lowers
 * to (lw_op[]), as parallel arrays indexed by IR op:
 *   L_SIMPLE   that opcode, with no operand
 *   L_COMPARE  that comparison, then NEG: PLASMA's comparisons give
 *              $FFFF for true, on every VM, and NEG makes it C's 1 (when
 *              the result only feeds a branch, the peephole stage removes
 *              the NEG)
 *   L_OPERAND  that opcode, with the IR op's operand a (a frame offset,
 *              or a branch's label)
 *   L_GLOBAL   that opcode, the absolute-address form, with a's address
 *              relocated as data (data_fix())
 *   L_PSEUDO   the pseudo-op $100 + that, with operand a
 *   L_SPECIAL  lowered by lower_special()
 * The stores keep their value (DLW, DLB, DAW, DAB): an assignment's
 * value can be used, and when it isn't, the following DROP lets the
 * peephole stage make the plain store (SLW, ...). DLB keeps the stored
 * byte, which is the value C gives an assignment to a char. */
enum { L_SIMPLE, L_COMPARE, L_OPERAND, L_GLOBAL, L_PSEUDO, L_SPECIAL };

static const char lw_kind[IR_NOPS] = {
  L_SPECIAL, L_SPECIAL, L_SPECIAL,          /* FUNC, ENDFUNC, CONST */
  L_GLOBAL, L_SPECIAL,                      /* DATAADDR, STRADDR */
  L_OPERAND, L_OPERAND, L_OPERAND, L_OPERAND, L_OPERAND,   /* locals */
  L_GLOBAL, L_GLOBAL, L_GLOBAL, L_GLOBAL,   /* globals */
  L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE,   /* LOAD ... STOREB */
  L_SPECIAL, L_SIMPLE, L_SIMPLE, L_SPECIAL, /* CALL, DROP, DUP, RETURN */
  L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE,        /* ADD ... */
  L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE,        /* ... SHR */
  L_COMPARE, L_COMPARE, L_COMPARE, L_COMPARE, L_COMPARE, L_COMPARE,
  L_COMPARE, L_COMPARE, L_COMPARE, L_COMPARE,   /* the unsigned (irin.c
                                                 * flips the sign bits first) */
  L_SIMPLE, L_SIMPLE, L_SPECIAL,            /* NEG, COMP, NOT */
  L_PSEUDO, L_OPERAND, L_OPERAND, L_OPERAND, L_OPERAND,    /* ... SEL */
  L_SPECIAL, L_PSEUDO, L_PSEUDO, L_PSEUDO,  /* JOIN, the case table */
  L_SPECIAL,                                /* SWAP */
  L_SPECIAL, L_SPECIAL, L_SPECIAL,          /* UDIV, UMOD, USHR */
  L_SPECIAL,                                /* HEAPADDR */
  L_SPECIAL, L_SPECIAL, L_SPECIAL, L_SPECIAL    /* loop markers: irin.c
                                                 * takes them */
};

static const unsigned char lw_op[IR_NOPS] = {
  0, 0, 0,
  OP_LA, 0,
  OP_LLW, OP_DLW, OP_LLB, OP_DLB, OP_LLA,
  OP_LAW, OP_DAW, OP_LAB, OP_DAB,
  OP_LW, OP_LB, OP_SW, OP_SB,
  0, OP_DROP, OP_DUP, 0,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
  OP_AND, OP_IOR, OP_XOR, OP_SHL, OP_SHR,
  OP_ISEQ, OP_ISNE, OP_ISLT, OP_ISGE, OP_ISGT, OP_ISLE,
  OP_ISLT, OP_ISGE, OP_ISGT, OP_ISLE,       /* the unsigned: the signed
                                             * opcode, after the flip */
  OP_NEG, OP_COMP, 0,
  OP_LABEL & 0xFF, OP_BRNCH, OP_BRFLS, OP_BRTRU, OP_SEL,
  0, OP_CASES & 0xFF, OP_CASEVAL & 0xFF, OP_CASETO & 0xFF,
  0,
  0, 0, 0,
  OP_LA,
  0, 0, 0, 0
};

/* Write s, then n in decimal, to standard output, for -i. No printf:
 * the small build has none. */
static void out_num(const char *s, int n)
{
  fputs(s, stdout);
  fputs(num_text(n), stdout);
}

/* Print one IR op, for -i: "IR", its name padded to 8 characters, and
 * its operands. */
void dump_ir(int op, int a, int f)
{
  int n = (int)strlen(ir_names[op]);
  fputs("IR ", stdout);
  fputs(ir_names[op], stdout);
  while (n++ < 8)
    fputs(" ", stdout);
  if (IR_IS_LOOP_MARKER(op))
    op = IR_JOIN;               /* a loop marker's two labels, the same */
  switch (op) {
  case IR_FUNC:     fputs(" ", stdout); fputs(func_name(f), stdout); break;
  case IR_ENDFUNC:  out_num(" frame=", a); break;
  case IR_CONST:    out_num(" ", a); break;
  case IR_DATAADDR: case IR_GLOBAL: case IR_SETGLOBAL:
  case IR_GLOBALB: case IR_SETGLOBALB:
                    if (f >= 0) {
                      out_num(" sym", f);
                      out_num("+", a);
                    } else {
                      out_num(" data+", a);
                    }
                    break;
  case IR_STRADDR:  out_num(" string+", a); break;
  case IR_HEAPADDR: out_num(" sym", f); break;
  case IR_LOCAL: case IR_SETLOCAL:
  case IR_LOCALB: case IR_SETLOCALB: case IR_LOCALADDR:
                    out_num(" [", a); fputs("]", stdout); break;
  case IR_CALL:     fputs(" ", stdout); fputs(func_name(f), stdout);
                    out_num(" nargs=", a); break;
  case IR_LABEL: case IR_JUMP: case IR_BRFALSE: case IR_BRTRUE:
  case IR_SEL: case IR_CASETO:
                    out_num(" L", a); break;
  case IR_JOIN:     out_num(" L", a); out_num(" L", f); break;
  case IR_CASES: case IR_CASEVAL:
                    out_num(" ", a); break;
  case IR_SWAP:     out_num(" ", a); out_num(" ", f); break;
  case IR_ULT: case IR_UGT: case IR_ULE: case IR_UGE:
  case IR_UDIV: case IR_UMOD: case IR_USHR:
                    out_num(" ", a); break;
  }
  fputs("\n", stdout);
}

/* The fixup for a global op's address: a data offset (f is -1), or
 * extern global f plus the addend in m->a, which m->fn then names. */
static int data_fix(Mop *m, int f)
{
  m->fn = f;
  return f < 0 ? FIX_DATA : FIX_DSYM;
}

/* Make m push the constant v, using the shortest encoding (the same
 * choice as the PLASMA compiler's emit_const). */
void mop_const(Mop *m, int v)
{
  memset(m, 0, sizeof(*m));
  v &= 0xFFFF;
  if (v == 0xFFFF) {
    m->op = OP_MINUS1;
  } else if (v < 16) {
    m->op = OP_CN + 2 * v;
  } else if (v <= 0xFF) {
    m->op = OP_CB;
    m->a = v;
  } else if (v >= 0xFF00) {
    m->op = OP_CFFB;
    m->a = v & 0xFF;
  } else {
    m->op = OP_CW;
    m->a = v;
  }
}

/* If m pushes a constant, store it (as 16 bits) in *v and return 1. */
int mop_const_value(Mop *m, int *v)
{
  if (m->op < 0x20 && !(m->op & 1)) {
    *v = m->op / 2;
    return 1;
  }
  switch (m->op) {
  case OP_MINUS1: *v = 0xFFFF;          return 1;
  case OP_CB:     *v = m->a;            return 1;
  case OP_CFFB:   *v = m->a | 0xFF00;   return 1;
  case OP_CW:     *v = m->a & 0xFFFF;   return 1;
  }
  return 0;
}

/* Pass an operand-free machine op to the peephole stage. */
static void put_simple(int op)
{
  Mop m;
  memset(&m, 0, sizeof(m));
  m.op = op;
  peep_put(&m);
}

/* Pass a frame op (LLW, SLW, ...) on one of the back end's own
 * temporaries, k, to the peephole stage. */
static void put_temp(int op, int k)
{
  Mop m;
  memset(&m, 0, sizeof(m));
  m.op = op;
  m.a = FRAME_TEMP(k);
  peep_put(&m);
}

/* Make m a call of helper k, which this object must then hold. */
static void helper_call(Mop *m, int k)
{
  m->op = OP_CALL;
  m->fix = FIX_HELPER;
  m->fn = k;
  helper_used |= 1 << k;
}

/* Lower the IR ops of kind L_SPECIAL into m (and any ops before it). */
static void lower_special(Mop *m, int op, int a, int f)
{
  switch (op) {
  case IR_FUNC:
    m->op = OP_FUNC_BEGIN;
    m->fn = f;
    peep_put(m);
    memset(m, 0, sizeof(*m));
    m->op = OP_ENTER;
    m->a = FRAME_LATER;
    m->fn = a;
    break;
  case IR_ENDFUNC:
    m->op = OP_FUNC_END;
    m->a = a;
    m->fn = f;
    break;
  case IR_CONST:
    mop_const(m, a);
    break;
  case IR_STRADDR:
    m->op = OP_LA;
    m->a = a;
    m->fix = FIX_STR;
    break;
  case IR_HEAPADDR:
    m->op = OP_LA;
    m->fix = FIX_HEAP;
    m->fn = f;
    break;
  case IR_CALL:
    m->op = OP_CALL;
    m->fix = FIX_FUNC;
    m->fn = f;
    break;
  case IR_JOIN:
    m->op = OP_JOIN;
    m->a = a;
    m->fn = f;
    break;
  case IR_RETURN:
    m->op = OP_LEAVE;
    m->a = FRAME_LATER;
    break;
  case IR_NOT:
    /* C's !x is 1 or 0; PLASMA's NOT gives $FFFF (true) or 0 on
     * every VM, so mask it: NOT then AND 1 gives 1 or 0. When the
     * result only feeds a truth test (a branch, or another NOT), the
     * peephole stage drops the ANDI, since $FFFF is already "true". */
    put_simple(OP_NOT);
    m->op = OP_ANDI;
    m->a = 1;
    break;
  case IR_USHR:
    /* x >> n unsigned. By a constant n (f; irin.c made / by 2^n one
     * too): PLASMA's SHR is arithmetic, so mask off the copies of the
     * sign bit it shifts in. By a variable: the helper. */
    if (f < 0) {
      helper_call(m, HELP_USHR);
    } else if (f == 0) {
      m->op = OP_SHR;           /* by 0: nothing to mask */
    } else {
      put_simple(OP_SHR);
      mop_const(m, 0xFFFF >> f);
      peep_put(m);
      memset(m, 0, sizeof(*m));
      m->op = OP_AND;
    }
    break;
  case IR_UDIV: case IR_UMOD:
    /* By other than a power of 2 (irin.c did those): the helper. */
    helper_call(m, op == IR_UDIV ? HELP_UDIV : HELP_UMOD);
    break;
  case IR_SWAP:
    /* What irin.c couldn't do by moving code: exchange the top value
     * (a = 1), or the top value and its copy (a = 2), with the value
     * below, through two temporaries. */
    put_temp(OP_SLW, 0);
    if (a == 2)
      put_simple(OP_DROP);
    put_temp(OP_SLW, 1);
    put_temp(OP_LLW, 0);
    if (a == 2)
      put_temp(OP_LLW, 0);
    m->op = OP_LLW;
    m->a = FRAME_TEMP(1);
    break;
  default:
    fatal(174 /* "internal error: unknown IR op" */, NULL);
  }
}

/* Stage 2's entry: lower one IR op to machine ops and pass them to the
 * peephole stage, as lw_kind[] and lw_op[] say. */
void lower_op(int op, int a, int f)
{
  Mop m;
  int kind;

  memset(&m, 0, sizeof(m));
  if (op == IR_MOP) {
    m.op = a;                   /* a counted loop's branch, to label f */
    m.a = f;
    peep_put(&m);
    return;
  }
  kind = lw_kind[op];
  m.op = lw_op[op];
  if (kind == L_COMPARE) {
    put_simple(m.op);
    m.op = OP_NEG;
  } else if (kind == L_SPECIAL) {
    lower_special(&m, op, a, f);
  } else if (kind != L_SIMPLE) {
    m.a = a;
    if (kind == L_PSEUDO)
      m.op += 0x100;
    else if (kind == L_GLOBAL)
      m.fix = data_fix(&m, f);
  }
  peep_put(&m);
  if (op == IR_ENDFUNC)
    peep_flush();
}
