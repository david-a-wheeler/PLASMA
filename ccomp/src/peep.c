/*
 * Stage 3: peephole optimizer. Holds a small window of machine ops,
 * rewrites patterns at the end of the window, and passes ops on to the
 * emitter.
 *
 * Dead code: after an unconditional exit (LEAVE, RET, BRNCH), nothing
 * can run until the next barrier, so ops are discarded until then.
 * Barriers are the pseudo-ops: function start and end, and labels (a
 * label is where a branch can land, which makes code reachable again).
 * This removes the implicit "return 0" the parser adds after a final
 * return statement, the jump after an if-part that ends in return, and
 * the bodies of if (0) and while (0).
 *
 * Patterns (each checked at the end of the window after every op):
 *   DLW n  DROP              -> SLW n      assignment whose value is unused
 *   SLW n  LLW n             -> DLW n      store, then load the same local
 *                                          (these two also for DLB, SLB, LLB)
 *   const  AND               -> ANDI const for a constant from 0 to 255
 *   const  NEG / COMP        -> const      e.g. -5 is one constant
 *   CN 1   ADD / SUB         -> INCR / DECR
 *   NEG  BRFLS / BRTRU       -> BRFLS / BRTRU    comparison used as a test
 *   NOT ANDI 1  BRFLS/BRTRU  -> BRTRU / BRFLS    branch on !x
 *   ISEQ / ISNE  BRFLS/BRTRU -> BREQ / BRNE
 *   CN 0  BREQ / BRNE        -> BRFLS / BRTRU    x == 0 is a test of x
 *   DUP  op  SLW n  DROP     -> op  SLW n  x++ whose old value is unused
 *                                          (also with SLB)
 *                                          (op is INCR, DECR, NEG or COMP)
 * All are valid for C: they don't change any value the program sees.
 */
#include "ccish.h"

#define WINDOW 6

static Mop win[WINDOW];
static int nwin;
static int dead;            /* code is unreachable until the next barrier */

/* Send the oldest op in the window on to stage 4. */
static void pass_oldest(void)
{
    int i;
    emit_put(&win[0]);
    for (i = 1; i < nwin; i++)
        win[i - 1] = win[i];
    nwin--;
}

/* Send every op in the window on to stage 4, oldest first. */
void peep_flush(void)
{
    while (nwin > 0)
        pass_oldest();
}

/* Does op always transfer control (a return or a jump), so that what
 * follows can't run until the next label? */
static int is_unconditional_exit(int op)
{
    return op == OP_LEAVE || op == OP_RET || op == OP_BRNCH;
}

/* Is op a branch that pops a value and tests it for zero? */
static int is_test_branch(int op)
{
    return op == OP_BRFLS || op == OP_BRTRU;
}

/* The branch with the opposite test: BRFLS for BRTRU, and so on. */
static int invert_branch(int op)
{
    return op == OP_BRFLS ? OP_BRTRU : OP_BRFLS;
}

/* Is op a one-operand op that can sit between DUP and SLW in the x++
 * pattern below? */
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

    /* DLW / DLB n DROP -> SLW / SLB n: an assignment whose value is
     * unused just stores, without keeping a copy to throw away. */
    if ((a->op == OP_DLW || a->op == OP_DLB) && b->op == OP_DROP) {
        a->op = a->op == OP_DLW ? OP_SLW : OP_SLB;
        nwin--;
        return 1;
    }
    /* SLW n LLW n -> DLW n (and the same for bytes): storing a local and
     * then loading it again becomes a store that keeps the value. DLB
     * keeps the stored byte, which is what LLB would load. */
    if (((a->op == OP_SLW && b->op == OP_LLW)
         || (a->op == OP_SLB && b->op == OP_LLB)) && a->a == b->a) {
        a->op = a->op == OP_SLW ? OP_DLW : OP_DLB;
        nwin--;
        return 1;
    }
    /* const AND -> ANDI const, for a constant from 0 to 255: one
     * two-byte op (it clears the high byte, as the AND would). */
    if (b->op == OP_AND && mop_const_value(a, &v) && v <= 0xFF) {
        a->op = OP_ANDI;
        a->a = v;
        nwin--;
        return 1;
    }
    /* const NEG / COMP -> const: negate or complement the constant at
     * compile time (a backstop for the parser's folding). */
    if ((b->op == OP_NEG || b->op == OP_COMP) && mop_const_value(a, &v)) {
        mop_const(a, b->op == OP_NEG ? -v : ~v);
        nwin--;
        return 1;
    }
    /* CN 1 ADD / SUB -> INCR / DECR: adding or subtracting 1 is a
     * single one-byte op. */
    if ((b->op == OP_ADD || b->op == OP_SUB)
        && mop_const_value(a, &v) && v == 1) {
        a->op = b->op == OP_ADD ? OP_INCR : OP_DECR;
        nwin--;
        return 1;
    }
    /* NEG BRFLS / BRTRU -> BRFLS / BRTRU: negating doesn't change
     * whether a value is 0, so a comparison's NEG (which makes C's 1)
     * isn't needed when the result only feeds a branch. */
    if (a->op == OP_NEG && is_test_branch(b->op)) {
        *a = *b;
        nwin--;
        return 1;
    }
    /* NOT ANDI 1 BRFLS / BRTRU -> BRTRU / BRFLS: branch on !x by
     * testing x with the opposite branch. */
    if (nwin >= 3 && win[nwin - 3].op == OP_NOT && a->op == OP_ANDI
        && a->a == 1 && is_test_branch(b->op)) {
        win[nwin - 3] = *b;
        win[nwin - 3].op = invert_branch(b->op);
        nwin -= 2;
        return 1;
    }
    /* ISEQ / ISNE then BRFLS / BRTRU -> BREQ / BRNE: compare and branch
     * in one op. */
    if ((a->op == OP_ISEQ || a->op == OP_ISNE) && is_test_branch(b->op)) {
        int on_true = b->op == OP_BRTRU;
        int on_equal = (a->op == OP_ISEQ) == on_true;
        *a = *b;
        a->op = on_equal ? OP_BREQ : OP_BRNE;
        nwin--;
        return 1;
    }
    /* CN 0 BREQ / BRNE -> BRFLS / BRTRU: comparing with 0 is just a
     * test of the value. */
    if ((b->op == OP_BREQ || b->op == OP_BRNE)
        && mop_const_value(a, &v) && v == 0) {
        *a = *b;
        a->op = b->op == OP_BREQ ? OP_BRFLS : OP_BRTRU;
        nwin--;
        return 1;
    }
    /* DUP op SLW / SLB n DROP -> op SLW / SLB n: x++ (or a similar
     * update) whose old value is unused needs no copy of it. */
    if (nwin >= 4 && b->op == OP_DROP && (a->op == OP_SLW || a->op == OP_SLB)
        && win[nwin - 4].op == OP_DUP && is_unary(win[nwin - 3].op)) {
        win[nwin - 4] = win[nwin - 3];
        win[nwin - 3] = *a;
        nwin -= 2;
        return 1;
    }
    return 0;
}

/* Stage 3's entry: add an op to the window and apply the patterns.
 * Barriers flush the window, and ops in dead code are dropped. */
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
