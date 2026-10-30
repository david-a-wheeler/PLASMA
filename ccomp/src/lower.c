/*
 * Stage 2: lowering. Turns IR ops into PLASMA machine ops and chooses
 * their encodings, mostly 1:1. Each IR op is described in ../IR.md.
 *
 * IR ops first wait in a small buffer, so that the parser can still
 * reorder the code of the current expression (approach.md 4(d2)). The
 * parser notes positions in the IR stream (ir_here()), and
 * ir_rotate() exchanges two adjacent blocks of ops that are still in
 * the buffer: *d++ = *s++ is parsed address first, but needs the value
 * first, since PLASMA's stores take the address on top and there is no
 * SWAP. Moving ops is safe because each lowers on its own and branches
 * name labels by number. The buffer is flushed at the end of each full
 * expression and of each function, and when it is full; then
 * ir_holds() tells the parser that code before the flush can no longer
 * move.
 */
#include <string.h>
#include "ccish.h"

static const char *const ir_names[] = {
  "FUNC", "ENDFUNC", "CONST", "DATAADDR", "STRADDR", "LOCAL", "SETLOCAL",
  "LOCALB", "SETLOCALB", "LOCALADDR", "GLOBAL", "SETGLOBAL", "GLOBALB",
  "SETGLOBALB", "LOAD", "LOADB", "STORE", "STOREB", "CALL",
  "DROP", "DUP", "RETURN",
  "ADD", "SUB", "MUL", "DIV", "MOD", "AND", "OR", "XOR", "SHL", "SHR",
  "EQ", "NE", "LT", "GT", "LE", "GE",
  "NEG", "COMP", "NOT",
  "LABEL", "JUMP", "BRFALSE", "BRTRUE", "JOIN",
  "SEL", "CASES", "CASEVAL", "CASETO"
};

/* PLASMA's comparison op for each IR comparison, or -1. */
static int compare_op(int op)
{
  switch (op) {
  case IR_EQ: return OP_ISEQ;
  case IR_NE: return OP_ISNE;
  case IR_LT: return OP_ISLT;
  case IR_GT: return OP_ISGT;
  case IR_LE: return OP_ISLE;
  case IR_GE: return OP_ISGE;
  }
  return -1;
}

/* PLASMA's branch op for each IR branch, or -1. */
static int branch_op(int op)
{
  switch (op) {
  case IR_JUMP:    return OP_BRNCH;
  case IR_BRFALSE: return OP_BRFLS;
  case IR_BRTRUE:  return OP_BRTRU;
  case IR_SEL:     return OP_SEL;
  }
  return -1;
}

/* PLASMA opcode for each IR op that maps to a single operand-free op. */
static int simple_op(int op)
{
  switch (op) {
  case IR_DROP: return OP_DROP;
  case IR_DUP:  return OP_DUP;
  case IR_LOAD:   return OP_LW;
  case IR_LOADB:  return OP_LB;
  case IR_STORE:  return OP_SW;
  case IR_STOREB: return OP_SB;
  case IR_ADD:  return OP_ADD;
  case IR_SUB:  return OP_SUB;
  case IR_MUL:  return OP_MUL;
  case IR_DIV:  return OP_DIV;
  case IR_MOD:  return OP_MOD;
  case IR_AND:  return OP_AND;
  case IR_OR:   return OP_IOR;
  case IR_XOR:  return OP_XOR;
  case IR_SHL:  return OP_SHL;
  case IR_SHR:  return OP_SHR;
  case IR_NEG:  return OP_NEG;
  case IR_COMP: return OP_COMP;
  }
  return -1;
}

/* Print one IR op, for -i. */
static void dump_ir(int op, int a, int f)
{
  printf("IR %-8s", ir_names[op]);
  switch (op) {
  case IR_FUNC:     printf(" %s", name_of(fn_name[f])); break;
  case IR_ENDFUNC:  printf(" frame=%d", a); break;
  case IR_CONST:    printf(" %d", a); break;
  case IR_DATAADDR: case IR_GLOBAL: case IR_SETGLOBAL:
  case IR_GLOBALB: case IR_SETGLOBALB:
                    if (f >= 0)
                      printf(" sym%d+%d", f, a);
                    else
                      printf(" data+%d", a);
                    break;
  case IR_STRADDR:  printf(" string+%d", a); break;
  case IR_LOCAL: case IR_SETLOCAL:
  case IR_LOCALB: case IR_SETLOCALB: case IR_LOCALADDR:
                    printf(" [%d]", a); break;
  case IR_CALL:     printf(" %s nargs=%d", name_of(fn_name[f]), a); break;
  case IR_LABEL: case IR_JUMP: case IR_BRFALSE: case IR_BRTRUE:
  case IR_SEL: case IR_CASETO:
                    printf(" L%d", a); break;
  case IR_JOIN:     printf(" L%d L%d", a, f); break;
  case IR_CASES: case IR_CASEVAL:
                    printf(" %d", a); break;
  }
  printf("\n");
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

/* Lower one IR op to machine ops and pass them to the peephole stage. */
static void lower(int op, int a, int f)
{
  Mop m;

  if (opt_ir)
    dump_ir(op, a, f);
  if (simple_op(op) >= 0) {
    put_simple(simple_op(op));
    return;
  }
  if (compare_op(op) >= 0) {
    /* PLASMA's comparisons give $FFFF for true, on every VM; NEG
     * makes that C's 1. When the result only feeds a branch, the
     * peephole stage removes the NEG. */
    put_simple(compare_op(op));
    put_simple(OP_NEG);
    return;
  }
  memset(&m, 0, sizeof(m));
  if (branch_op(op) >= 0) {
    m.op = branch_op(op);
    m.a = a;
    peep_put(&m);
    return;
  }
  switch (op) {
  case IR_FUNC:
    m.op = OP_FUNC_BEGIN;
    m.fn = f;
    peep_put(&m);
    memset(&m, 0, sizeof(m));
    m.op = OP_ENTER;
    m.a = FRAME_LATER;
    m.b = a;
    break;
  case IR_ENDFUNC:
    m.op = OP_FUNC_END;
    m.a = a;
    m.fn = f;
    break;
  case IR_CONST:
    mop_const(&m, a);
    break;
  case IR_DATAADDR:
  case IR_STRADDR:
    m.op = OP_LA;
    m.a = a;
    m.fix = op == IR_STRADDR ? FIX_STR : data_fix(&m, f);
    break;
  case IR_LOCAL:
    m.op = OP_LLW;
    m.a = a;
    break;
  case IR_SETLOCAL:
    /* Store and keep the value: an assignment's value can be used.
     * When it isn't, the following DROP lets the peephole stage
     * turn DLW + DROP into SLW. */
    m.op = OP_DLW;
    m.a = a;
    break;
  case IR_LOCALB:
    m.op = OP_LLB;
    m.a = a;
    break;
  case IR_SETLOCALB:
    /* DLB keeps the stored byte, which is the value C gives an
     * assignment to a char. */
    m.op = OP_DLB;
    m.a = a;
    break;
  case IR_LOCALADDR:
    m.op = OP_LLA;
    m.a = a;
    break;
  case IR_GLOBAL: case IR_SETGLOBAL: case IR_GLOBALB: case IR_SETGLOBALB:
    /* The absolute-address forms of LLW, DLW, LLB and DLB, with the
     * address relocated as data. As for locals, a store keeps its
     * value, and DAW + DROP becomes SAW. */
    m.op = op == IR_GLOBAL ? OP_LAW : op == IR_SETGLOBAL ? OP_DAW
         : op == IR_GLOBALB ? OP_LAB : OP_DAB;
    m.a = a;
    m.fix = data_fix(&m, f);
    break;
  case IR_CALL:
    m.op = OP_CALL;
    m.fix = FIX_FUNC;
    m.fn = f;
    break;
  case IR_LABEL:
    m.op = OP_LABEL;
    m.a = a;
    break;
  case IR_JOIN:
    m.op = OP_JOIN;
    m.a = a;
    m.b = f;
    break;
  case IR_CASES: case IR_CASEVAL: case IR_CASETO:
    m.op = op == IR_CASES ? OP_CASES
         : op == IR_CASEVAL ? OP_CASEVAL : OP_CASETO;
    m.a = a;
    break;
  case IR_RETURN:
    m.op = OP_LEAVE;
    m.a = FRAME_LATER;
    break;
  case IR_NOT:
    /* C's !x is 1 or 0. PLASMA's NOT gives $FFFF (6502 VMs) or 1
     * (C VM) for true, so mask it: NOT then AND 1 gives 1 or 0 on
     * every VM, in two ops. */
    put_simple(OP_NOT);
    m.op = OP_ANDI;
    m.a = 1;
    break;
  default:
    fatal(174 /* "internal error: unknown IR op" */, NULL);
  }
  peep_put(&m);
  if (op == IR_ENDFUNC)
    peep_flush();
}

/* The IR ops waiting to be lowered. 64 is far more than a typical
 * statement needs (*d++ = *s++ is 9); see approach.md 4(d2). */
#define IR_BUF 64

typedef struct {
  int op, a;
  int f;          /* a function, or -1 */
} IrOp;

static IrOp buf[IR_BUF];
static int nbuf;
static int base;            /* the IR position of buf[0] */

/* Lower every buffered op, oldest first. Positions keep counting from
 * where they were, so that a mark taken before this is seen as gone. */
static void lower_all(void)
{
  int i;
  for (i = 0; i < nbuf; i++)
    lower(buf[i].op, buf[i].a, buf[i].f);
  base += nbuf;
  nbuf = 0;
}

/* End of a full expression (or a function): lower every buffered op.
 * No mark can be in use, so positions start again from 0. */
void ir_flush(void)
{
  lower_all();
  base = 0;
}

/* The IR position just after the last op put: where the next op will
 * go. */
int ir_here(void)
{
  return base + nbuf;
}

/* Is the op at IR position pos (or the position itself, if nothing has
 * been put there yet) still in the buffer, so that it can be moved? */
int ir_holds(int pos)
{
  return pos >= base;
}

/* Reverse the buffered ops from index i up to (not including) j. */
static void reverse(int i, int j)
{
  IrOp t;
  while (i < --j) {
    t = buf[i];
    buf[i++] = buf[j];
    buf[j] = t;
  }
}

/* Exchange the block of ops from position m0 to m1 with the block from
 * m1 to the end, by reversing each, then both together (Bentley,
 * Programming Pearls, column 2). The caller checks ir_holds(m0). */
void ir_rotate(int m0, int m1)
{
  int i = m0 - base, j = m1 - base;
  if (i < 0 || i > j || j > nbuf)
    fatal(175 /* "internal error: bad IR rotation" */, NULL);
  reverse(i, j);
  reverse(j, nbuf);
  reverse(i, nbuf);
}

/* Stage 2's entry: buffer one IR op. A full buffer is lowered first,
 * and the end of a function flushes everything. */
void ir_put(int op, int a, int f)
{
  if (nbuf == IR_BUF)
    lower_all();
  buf[nbuf].op = op;
  buf[nbuf].a = a;
  buf[nbuf].f = f;
  nbuf++;
  if (op == IR_ENDFUNC)
    ir_flush();
}
