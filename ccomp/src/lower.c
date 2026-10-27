/*
 * Stage 2: lowering. Turns IR ops into PLASMA machine ops and chooses
 * their encodings. For milestone 1 this is a 1:1 translation; later it
 * becomes the expression-sized delay buffer of approach.md 4(d).
 */
#include <string.h>
#include "ccish.h"

static const char *const ir_names[] = {
    "FUNC", "ENDFUNC", "CONST", "DATAADDR", "LOCAL", "ADD", "CALL",
    "DROP", "RETURN"
};

static void dump_ir(int op, int a, Sym *s)
{
    printf("IR %-8s", ir_names[op]);
    switch (op) {
    case IR_FUNC:     printf(" %s", s->name); break;
    case IR_ENDFUNC:  printf(" frame=%d", a); break;
    case IR_CONST:    printf(" %d", a); break;
    case IR_DATAADDR: printf(" data+%d", a); break;
    case IR_LOCAL:    printf(" [%d]", a); break;
    case IR_CALL:     printf(" %s nargs=%d", s->name, a); break;
    }
    printf("\n");
}

/* Push a constant, using the shortest encoding (the same choice as the
 * PLASMA compiler's emit_const). */
static void lower_const(Mop *m, int v)
{
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

void ir_put(int op, int a, Sym *s)
{
    Mop m;

    if (opt_ir)
        dump_ir(op, a, s);
    memset(&m, 0, sizeof(m));
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
        lower_const(&m, a);
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
    case IR_ADD:
        m.op = OP_ADD;
        break;
    case IR_CALL:
        m.op = OP_CALL;
        m.fix = FIX_FUNC;
        m.sym = s;
        break;
    case IR_DROP:
        m.op = OP_DROP;
        break;
    case IR_RETURN:
        m.op = OP_LEAVE;
        m.a = FRAME_LATER;
        break;
    default:
        fatal("internal error: unknown IR op", NULL);
    }
    peep_put(&m);
    if (op == IR_ENDFUNC)
        peep_flush();
}
