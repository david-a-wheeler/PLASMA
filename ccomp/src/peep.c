/*
 * Stage 3: peephole optimizer. Holds a small window of machine ops,
 * rewrites patterns at the end of the window, and passes ops on to the
 * emitter.
 *
 * Dead code: after an unconditional exit (LEAVE, RET), nothing can run
 * until the next barrier, so ops are discarded until then. Barriers
 * are the pseudo-ops (function start and end) and, from M3, labels:
 * a label is where a jump can land, which makes code reachable again.
 * This is also what removes the implicit "return 0" the parser adds
 * after a final return statement.
 *
 * Patterns (each checked at the end of the window after every op):
 *   DLW n  DROP              -> SLW n      assignment whose value is unused
 *   SLW n  LLW n             -> DLW n      store, then load the same local
 *   const  NEG / COMP        -> const      e.g. -5 is one constant
 *   CN 1   ADD / SUB         -> INCR / DECR
 *   DUP  op  SLW n  DROP     -> op  SLW n  x++ whose old value is unused
 *                                          (op is INCR, DECR, NEG or COMP)
 * All are valid for C: they don't change any value the program sees.
 */
#include "ccish.h"

#define WINDOW 6

static Mop win[WINDOW];
static int nwin;
static int dead;            /* code is unreachable until the next barrier */

static void pass_oldest(void)
{
    int i;
    emit_put(&win[0]);
    for (i = 1; i < nwin; i++)
        win[i - 1] = win[i];
    nwin--;
}

void peep_flush(void)
{
    while (nwin > 0)
        pass_oldest();
}

static int is_unconditional_exit(int op)
{
    return op == OP_LEAVE || op == OP_RET;
}

static int is_unary(int op)
{
    return op == OP_INCR || op == OP_DECR || op == OP_NEG || op == OP_COMP;
}

/* Try one pattern at the end of the window. Returns 1 if it changed
 * anything, so the caller tries again. */
static int rewrite(void)
{
    Mop *a, *b;
    int v;

    if (nwin < 2)
        return 0;
    a = &win[nwin - 2];
    b = &win[nwin - 1];

    if (a->op == OP_DLW && b->op == OP_DROP) {
        a->op = OP_SLW;
        nwin--;
        return 1;
    }
    if (a->op == OP_SLW && b->op == OP_LLW && a->a == b->a) {
        a->op = OP_DLW;
        nwin--;
        return 1;
    }
    if ((b->op == OP_NEG || b->op == OP_COMP) && mop_const_value(a, &v)) {
        mop_const(a, b->op == OP_NEG ? -v : ~v);
        nwin--;
        return 1;
    }
    if ((b->op == OP_ADD || b->op == OP_SUB)
        && mop_const_value(a, &v) && v == 1) {
        a->op = b->op == OP_ADD ? OP_INCR : OP_DECR;
        nwin--;
        return 1;
    }
    if (nwin >= 4 && b->op == OP_DROP && a->op == OP_SLW
        && win[nwin - 4].op == OP_DUP && is_unary(win[nwin - 3].op)) {
        win[nwin - 4] = win[nwin - 3];
        win[nwin - 3] = *a;
        nwin -= 2;
        return 1;
    }
    return 0;
}

void peep_put(Mop *m)
{
    if (m->op >= OP_FUNC_BEGIN) {
        /* A barrier: never optimize across it. */
        peep_flush();
        dead = 0;
        emit_put(m);
        return;
    }
    if (dead)
        return;
    if (nwin == WINDOW)
        pass_oldest();
    win[nwin++] = *m;
    if (is_unconditional_exit(m->op))
        dead = 1;
    while (rewrite())
        ;
}
