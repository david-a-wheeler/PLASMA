/*
 * Stage 2: lowering. Turns IR ops into PLASMA machine ops and chooses
 * their encodings. For now this is mostly a 1:1 translation; later it
 * becomes the expression-sized delay buffer of approach.md 4(d).
 * Each IR op is described in ../IR.md.
 */
#include <string.h>
#include "ccish.h"

static const char *const ir_names[] = {
    "FUNC", "ENDFUNC", "CONST", "DATAADDR", "LOCAL", "SETLOCAL", "CALL",
    "DROP", "DUP", "RETURN",
    "ADD", "SUB", "MUL", "DIV", "MOD", "AND", "OR", "XOR", "SHL", "SHR",
    "EQ", "NE", "LT", "GT", "LE", "GE",
    "NEG", "COMP", "NOT",
    "LABEL", "JUMP", "BRFALSE", "BRTRUE", "BRAND", "BROR"
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
    case IR_BRAND:   return OP_BRAND;
    case IR_BROR:    return OP_BROR;
    }
    return -1;
}

/* PLASMA opcode for each IR op that maps to a single operand-free op. */
static int simple_op(int op)
{
    switch (op) {
    case IR_DROP: return OP_DROP;
    case IR_DUP:  return OP_DUP;
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
static void dump_ir(int op, int a, Sym *s)
{
    printf("IR %-8s", ir_names[op]);
    switch (op) {
    case IR_FUNC:     printf(" %s", s->name); break;
    case IR_ENDFUNC:  printf(" frame=%d", a); break;
    case IR_CONST:    printf(" %d", a); break;
    case IR_DATAADDR: printf(" data+%d", a); break;
    case IR_LOCAL:
    case IR_SETLOCAL: printf(" [%d]", a); break;
    case IR_CALL:     printf(" %s nargs=%d", s->name, a); break;
    case IR_LABEL: case IR_JUMP: case IR_BRFALSE: case IR_BRTRUE:
    case IR_BRAND: case IR_BROR:
                      printf(" L%d", a); break;
    }
    printf("\n");
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

/* Stage 2's entry: lower one IR op to machine ops and pass them to the
 * peephole stage. */
void ir_put(int op, int a, Sym *s)
{
    Mop m;

    if (opt_ir)
        dump_ir(op, a, s);
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
        m.sym = s;
        peep_put(&m);
        memset(&m, 0, sizeof(m));
        m.op = OP_ENTER;
        m.a = FRAME_LATER;
        m.b = a;
        break;
    case IR_ENDFUNC:
        m.op = OP_FUNC_END;
        m.a = a;
        m.sym = s;
        break;
    case IR_CONST:
        mop_const(&m, a);
        break;
    case IR_DATAADDR:
        m.op = OP_LA;
        m.a = a;
        m.fix = FIX_DATA;
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
    case IR_CALL:
        m.op = OP_CALL;
        m.fix = FIX_FUNC;
        m.sym = s;
        break;
    case IR_LABEL:
        m.op = OP_LABEL;
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
        fatal("internal error: unknown IR op", NULL);
    }
    peep_put(&m);
    if (op == IR_ENDFUNC)
        peep_flush();
}
