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
 *                                          (these two also for bytes, and
 *                                          for globals: DAW, SAW, LAW, ...)
 *   const  AND               -> ANDI const for a constant from 0 to 255
 *   const  NEG / COMP        -> const      e.g. -5 is one constant
 *   CN 0   ADD / SUB         -> (nothing)
 *   CN 1   ADD / SUB         -> INCR / DECR
 *   NEG  BRFLS / BRTRU       -> BRFLS / BRTRU    comparison used as a test
 *   NOT ANDI 1  BRFLS/BRTRU  -> BRTRU / BRFLS    branch on !x
 *   ISEQ / ISNE  BRFLS/BRTRU -> BREQ / BRNE
 *   CN 0  BREQ / BRNE        -> BRFLS / BRTRU    x == 0 is a test of x
 *   CN 2  MUL                -> DUP ADD    (faster than CN 1 SHL; see rewrite)
 *   CN 3  MUL                -> DUP IDXW
 *   CN 0  MUL                -> DROP CN 0
 *   CN 1  MUL                -> (nothing)
 *   const MUL, const = 2^n   -> CN n SHL   for n from 2 to 15
 *   DUP  ADD  ADD            -> IDXW       p + 2*i
 *   DUP  update  store  DROP -> update  store   x++ whose old value is
 *        unused. update is INCR, DECR, NEG, COMP, or a constant then ADD
 *        or SUB; store is SLW, SLB, SAW or SAB n, or SW or SB after an
 *        address computed only on the stack (LLW p, LLA a LLW i IDXW, LA g
 *        LAW i IDXW, ...).
 * All are valid for C: they don't change any value the program sees.
 */
#include "ccish.h"

/* Big enough for an unused a[i]++ through a pointer with an offset:
 * DUP INCR LLW p LLW i IDXW CB d ADD SW DROP. */
#define WINDOW 10

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

/* The store that doesn't keep its value for a store that does (SLW for
 * DLW, and so on for DLB, DAW and DAB), or -1. */
static int plain_store(int op)
{
    switch (op) {
    case OP_DLW: return OP_SLW;
    case OP_DLB: return OP_SLB;
    case OP_DAW: return OP_SAW;
    case OP_DAB: return OP_SAB;
    }
    return -1;
}

/* The store that keeps its value, if load reads back what store wrote
 * (DLW for SLW then LLW, and so on for bytes and globals), or -1. The
 * caller checks that the two have the same address. */
static int keeping_store(int store, int load)
{
    if (store == OP_SLW && load == OP_LLW) return OP_DLW;
    if (store == OP_SLB && load == OP_LLB) return OP_DLB;
    if (store == OP_SAW && load == OP_LAW) return OP_DAW;
    if (store == OP_SAB && load == OP_LAB) return OP_DAB;
    return -1;
}

/* Is op a one-operand op that can sit between DUP and SLW in the x++
 * pattern below? */
static int is_unary(int op)
{
    return op == OP_INCR || op == OP_DECR || op == OP_NEG || op == OP_COMP;
}

/* If m is an op that only works on the stack (no branch, call or
 * store), set how many values it pops and pushes and return 1. */
static int stack_effect(Mop *m, int *pops, int *pushes)
{
    int v;
    *pushes = 1;
    if (mop_const_value(m, &v)) {
        *pops = 0;
        return 1;
    }
    switch (m->op) {
    case OP_LA: case OP_LLA: case OP_LLW: case OP_LLB: case OP_LAW:
    case OP_LAB:
        *pops = 0;
        return 1;
    case OP_LW: case OP_LB: case OP_INCR: case OP_DECR: case OP_NEG:
    case OP_COMP: case OP_ANDI:
        *pops = 1;
        return 1;
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_IDXW: case OP_SHL:
    case OP_AND: case OP_IOR: case OP_XOR:
        *pops = 2;
        return 1;
    case OP_DUP:
        *pops = 1;
        *pushes = 2;
        return 1;
    }
    return 0;
}

/* The number of ops in the self-contained expression that ends just
 * before window index end and pushes one value (such as the address
 * LLA a LLW i IDXW), or 0 if there isn't one. It scans backward,
 * counting the values still needed. */
static int expr_len(int end)
{
    int need = 1, i, pops, pushes;
    for (i = end - 1; i >= 0; i--) {
        if (!stack_effect(&win[i], &pops, &pushes) || need < pushes)
            return 0;
        need += pops - pushes;
        if (need == 0)
            return end - i;
    }
    return 0;
}

/* The number of ops in the store that ends just before window index
 * end: SLW / SLB / SAW / SAB n, or an address expression then SW / SB.
 * 0 if there isn't one. */
static int store_len(int end)
{
    int op = end >= 1 ? win[end - 1].op : -1, n;
    if (op == OP_SLW || op == OP_SLB || op == OP_SAW || op == OP_SAB)
        return 1;
    if (op == OP_SW || op == OP_SB) {
        n = expr_len(end - 1);
        return n ? n + 1 : 0;
    }
    return 0;
}

/* The number of ops (1 or 2) in the update that ends just before window
 * index end: a unary op, or a constant then ADD / SUB. 0 if none. */
static int update_len(int end)
{
    int v;
    if (end >= 1 && is_unary(win[end - 1].op))
        return 1;
    if (end >= 2 && (win[end - 1].op == OP_ADD || win[end - 1].op == OP_SUB)
        && mop_const_value(&win[end - 2], &v))
        return 2;
    return 0;
}

/* With DROP at the end of the window, match DUP update store DROP (an
 * x++ whose old value is unused) and remove the DUP and the DROP.
 * Returns 1 if it did. */
static int drop_dup(void)
{
    int s = store_len(nwin - 1), u, d, i;
    if (s == 0)
        return 0;
    u = update_len(nwin - 1 - s);
    d = nwin - 2 - s - u;           /* where the DUP must be */
    if (u == 0 || d < 0 || win[d].op != OP_DUP)
        return 0;
    for (i = d; i < nwin - 2; i++)
        win[i] = win[i + 1];
    nwin -= 2;
    return 1;
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

    /* DLW n DROP -> SLW n (also DLB, DAW, DAB): an assignment whose
     * value is unused just stores, without keeping a copy to throw away. */
    if (plain_store(a->op) >= 0 && b->op == OP_DROP) {
        a->op = plain_store(a->op);
        nwin--;
        return 1;
    }
    /* SLW n LLW n -> DLW n (also for bytes, and SAW / SAB with LAW / LAB):
     * storing a variable and then loading it again becomes a store that
     * keeps the value. DLB keeps the stored byte, which is what LLB would
     * load. */
    if (keeping_store(a->op, b->op) >= 0 && a->a == b->a) {
        a->op = keeping_store(a->op, b->op);
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
    /* CN 0 ADD / SUB -> nothing: adding or subtracting 0 changes
     * nothing (p[0] when p is itself loaded, as in names[i][0]). */
    if ((b->op == OP_ADD || b->op == OP_SUB)
        && mop_const_value(a, &v) && v == 0) {
        nwin -= 2;
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
    /* const MUL -> cheaper ops for some constants, as PLASMA's own
     * optimizer does (src/toolsrc/codeopt.pla). MUL is a 16-step
     * shift-and-add loop on the 6502 VMs, so it is the slowest choice. */
    if (b->op == OP_MUL && mop_const_value(a, &v)) {
        /* x * 2 -> DUP ADD, not CN 1 SHL. Both are 2 bytes, but on the
         * 6502 VM DUP ADD takes about 52 cycles and CN 1 SHL about 59:
         * SHL is a general shifter that saves Y, checks for counts of 8
         * or more, and loops. (codeopt.pla even turns CN 1 SHL into
         * DUP ADD.) It also lets p + i fuse into IDXW, below. */
        if (v == 2) {
            a->op = OP_DUP;
            a->a = 0;
            b->op = OP_ADD;
            return 1;
        }
        /* x * 3 -> DUP IDXW: x + 2x, in 2 bytes. */
        if (v == 3) {
            a->op = OP_DUP;
            a->a = 0;
            b->op = OP_IDXW;
            return 1;
        }
        /* x * 0 -> DROP CN 0: x is still computed, for its side
         * effects (such as a call), then replaced by 0. */
        if (v == 0) {
            a->op = OP_DROP;
            mop_const(b, 0);
            return 1;
        }
        /* x * 1 -> x: nothing to do. */
        if (v == 1) {
            nwin -= 2;
            return 1;
        }
        /* x * 2^n, n from 2 to 15 -> CN n SHL: the same size as the
         * MUL, and much faster. (Not for x / 2^n: SHR rounds toward
         * minus infinity, but C's / truncates toward zero.) */
        if ((v & (v - 1)) == 0) {
            unsigned u = (unsigned)v;
            int n = 0;
            while (u > 1) {
                u >>= 1;
                n++;
            }
            mop_const(a, n);
            b->op = OP_SHL;
            return 1;
        }
    }
    /* DUP ADD ADD -> IDXW: adding twice x is PLASMA's word index op
     * (p + i for a pointer to 2-byte elements). */
    if (nwin >= 3 && win[nwin - 3].op == OP_DUP && a->op == OP_ADD
        && b->op == OP_ADD) {
        win[nwin - 3].op = OP_IDXW;
        nwin -= 2;
        return 1;
    }
    /* DUP update store DROP -> update store: x++ (or a similar update)
     * whose old value is unused needs no copy of it. */
    if (b->op == OP_DROP && drop_dup())
        return 1;
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
