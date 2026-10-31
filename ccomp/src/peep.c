/*
 * Stage 3: peephole optimizer. Holds a small window of machine ops,
 * rewrites patterns at the end of the window, and passes ops on to the
 * emitter.
 *
 * Dead code: after an unconditional exit (LEAVE, RET, BRNCH, SEL),
 * nothing can run until the next barrier, so ops are discarded until
 * then. Barriers are the pseudo-ops: function start and end, labels (a
 * label is where a branch can land, which makes code reachable again),
 * and a SEL's case table (the code after it runs when no case matches).
 * This removes the implicit "return 0" the parser adds after a final
 * return statement, the jump after an if-part that ends in return, and
 * the bodies of if (0) and while (0).
 *
 * Patterns (each checked at the end of the window after every op):
 *   DLW n  DROP              -> SLW n      assignment whose value is unused
 *   SLW n  LLW n             -> DLW n      store, then load the same local
 *                                          (these two also for bytes, and
 *                                          for globals: DAW, SAW, LAW, ...)
 *   CW a   LB / LW / SB / SW -> LAB / LAW / SAB / SAW a   constant address
 *                                          (also after CB, CFFB, or LA)
 *   const  AND               -> ANDI const for a constant from 0 to 255
 *   const  NEG / COMP        -> const      e.g. -5 is one constant
 *   CN 0   ADD / SUB         -> (nothing)
 *   CN 1   ADD / SUB         -> INCR / DECR   (also MINUS1 SUB, ...)
 *   const  ADD / SUB         -> ADDI / SUBI n  for adding n from 2 to 255
 *                                          either way (x - 5, x + -5:
 *                                          SUBI 5)
 *   LLW n  ADD / IDXW        -> ADDLW / IDXLW n  (also LLB, LAW, LAB:
 *                                          ADDLB, IDXLB, ADDAW, ...)
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
 *        unused. update is INCR, DECR, NEG, COMP, ADDI, SUBI, or a
 *        constant then ADD or SUB; store is SLW, SLB, SAW or SAB n, or SW
 *        or SB after an address computed only on the stack (LLW p, LLA a
 *        IDXLW i, LA g IDXAW i, ...).
 * And at a label L, just before it flushes the window (skipping joins):
 *   BRNCH L                  -> (nothing)  a jump to the very next op
 *   Bcond L  BRNCH T         -> Binv T     the opposite branch (BRFLS /
 *                                          BRTRU, BREQ / BRNE); for
 *                                          a && b || c, !(a && b), ...
 * A join (OP_JOIN, from && and ||) emits no code: it isn't a barrier,
 * but waits in the window in order, even in dead code.
 * All are valid for C: they don't change any value the program sees.
 */
#include "build.h"
#include "cclimits.h"
#include "obj.h"
#include "mop.h"
#include "lower.h"
#include "peep.h"
#include "emit.h"

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
  return op == OP_LEAVE || op == OP_RET || op == OP_BRNCH || op == OP_SEL;
}

/* Is op a branch that pops a value and tests it for zero? */
static int is_test_branch(int op)
{
  return op == OP_BRFLS || op == OP_BRTRU;
}

/* The branch with the opposite test: BRFLS for BRTRU, BREQ for BRNE,
 * and back; or -1 for any other op. */
static int invert_branch(int op)
{
  switch (op) {
  case OP_BRFLS: return OP_BRTRU;
  case OP_BRTRU: return OP_BRFLS;
  case OP_BREQ:  return OP_BRNE;
  case OP_BRNE:  return OP_BREQ;
  }
  return -1;
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

/* The absolute-address form of a load or store through an address on
 * the stack (LAB for LB, and so on), or -1. */
static int absolute_op(int op)
{
  switch (op) {
  case OP_LB: return OP_LAB;
  case OP_LW: return OP_LAW;
  case OP_SB: return OP_SAB;
  case OP_SW: return OP_SAW;
  }
  return -1;
}

/* The op that adds a load's value (ADDLW for LLW, and so on for LLB,
 * LAW and LAB), or that adds twice it if idx (IDXLW, ...), or -1. The
 * VM numbers both sets of four in the loads' order: ADDLB is LLB +
 * $4C, and IDXLB is ADDLB + 8. */
static int fused_op(int load, int idx)
{
  if (load < OP_LLB || load > OP_LAW)
    return -1;
  return load + (OP_ADDLB - OP_LLB) + (idx ? OP_IDXLB - OP_ADDLB : 0);
}

/* Does op take one value and give one back, adding something to it
 * (ADDI, SUBI, or ADDLB to IDXAW)? */
static int is_add_op(int op)
{
  return op == OP_ADDI || op == OP_SUBI
         || (op >= OP_ADDLB && op <= OP_IDXAW);
}

/* Is op a one-operand op that can sit between DUP and SLW in the x++
 * pattern below? */
static int is_unary(int op)
{
  return op == OP_INCR || op == OP_DECR || op == OP_NEG || op == OP_COMP
         || op == OP_ADDI || op == OP_SUBI;
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
  if (is_add_op(m->op)) {
    *pops = 1;
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

/* The patterns on loads and stores, with a and b the last two ops in
 * the window. Returns 1 if one changed anything. */
static int rewrite_load_store(Mop *a, Mop *b)
{
  int v;

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
  if (keeping_store(a->op, b->op) >= 0 && a->a == b->a
      && a->fix == b->fix && a->fn == b->fn) {
    a->op = keeping_store(a->op, b->op);
    nwin--;
    return 1;
  }
  /* CW addr LB -> LAB addr (also LW, SB, SW, after CB or CFFB, or LA
   * for a data address): a load or store through a constant address
   * is one op, such as reading the speaker at $C030. Not after CN or
   * MINUS1, which are shorter than LAB's address word. */
  if (absolute_op(b->op) >= 0 && (a->op == OP_CW || a->op == OP_CB
                                  || a->op == OP_CFFB || a->op == OP_LA)) {
    if (a->op != OP_LA) {
      mop_const_value(a, &v);
      a->a = v;
    }
    a->op = absolute_op(b->op);
    nwin--;
    return 1;
  }
  /* LLW n ADD -> ADDLW n, and LLW n IDXW -> IDXLW n (also for LLB, LAW
   * and LAB): adding a variable's value, or twice it, is one op, a
   * byte smaller. */
  if ((b->op == OP_ADD || b->op == OP_IDXW)
      && fused_op(a->op, b->op == OP_IDXW) >= 0) {
    a->op = fused_op(a->op, b->op == OP_IDXW);
    nwin--;
    return 1;
  }
  return 0;
}

/* The patterns on a constant (a) and the op that uses it (b). Returns
 * 1 if one changed anything. */
static int rewrite_const(Mop *a, Mop *b)
{
  int v;

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
  /* const ADD / SUB, adding d (the constant, or minus it for SUB):
   * d = 0 -> nothing (p[0] when p is itself loaded, as in
   * names[i][0]); 1 or -1 -> INCR or DECR; up to 255 either way ->
   * ADDI or SUBI (x - 5 and x + -5 are both SUBI 5). One op instead of
   * two: a byte smaller after CB or CFFB, and faster after CN. */
  if ((b->op == OP_ADD || b->op == OP_SUB) && a->fix == FIX_NONE
      && mop_const_value(a, &v)) {
    int d = b->op == OP_ADD ? v : -v, op = OP_ADDI;
    if ((d & 0xFF00) != 0) {
      d = -d;
      op = OP_SUBI;
      if ((d & 0xFF00) != 0)
        return 0;
    }
    d &= 0xFF;
    if (d == 0) {
      nwin -= 2;
      return 1;
    }
    a->op = d != 1 ? op : op == OP_ADDI ? OP_INCR : OP_DECR;
    a->a = d;
    nwin--;
    return 1;
  }
  return 0;
}

/* The patterns that end in a branch (b), with a the op before it.
 * Returns 1 if one changed anything. */
static int rewrite_branch(Mop *a, Mop *b)
{
  int v;

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
  return 0;
}

/* const MUL -> cheaper ops for some constants, as PLASMA's own
 * optimizer does (src/toolsrc/codeopt.pla), with a the constant and b
 * the MUL. MUL is a 16-step shift-and-add loop on the 6502 VMs, so it
 * is the slowest choice. Returns 1 if it changed anything. */
static int rewrite_mul(Mop *a, Mop *b)
{
  int v;

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
  return 0;
}

/* Try one pattern at the end of the window. Returns 1 if it changed
 * anything, so the caller tries again. */
static int rewrite(void)
{
  Mop *a, *b;

  if (nwin < 2)
    return 0;
  a = &win[nwin - 2];
  b = &win[nwin - 1];
  if (rewrite_load_store(a, b) || rewrite_const(a, b)
      || rewrite_branch(a, b) || rewrite_mul(a, b))
    return 1;
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

/* The index of the last op in the window before index i that isn't a
 * join (which emits no code), or -1. */
static int code_before(int i)
{
  while (--i >= 0 && win[i].op == OP_JOIN)
    ;
  return i;
}

/* Remove the op at window index i. */
static void remove_op(int i)
{
  for (; i < nwin - 1; i++)
    win[i] = win[i + 1];
  nwin--;
}

/* The patterns at label n, just before it is placed (which flushes the
 * window). Joins in between are skipped. */
static void before_label(int n)
{
  int b = code_before(nwin), a;
  if (b < 0 || win[b].op != OP_BRNCH)
    return;
  /* BRNCH n, then label n -> the label alone: the jump goes to the
   * very next op. */
  if (win[b].a == n) {
    remove_op(b);
    return;
  }
  /* Bcond n BRNCH t, then label n -> Binv t, then label n: branch to t
   * on the opposite test, else fall through to n (BRFLS / BRTRU,
   * BREQ / BRNE). a && b || c and !(a && b) need this BRNCH. */
  a = code_before(b);
  if (a >= 0 && invert_branch(win[a].op) >= 0 && win[a].a == n) {
    win[a].op = invert_branch(win[a].op);
    win[a].a = win[b].a;
    remove_op(b);
  }
}

/* Stage 3's entry: add an op to the window and apply the patterns.
 * Barriers flush the window, and ops in dead code are dropped. A join
 * emits no code, so it isn't a barrier: it waits in the window, in
 * order (stage 4 needs the branches it moves first), and is kept even
 * in dead code (the branches it moves are live). */
void peep_put(Mop *m)
{
  if (m->op == OP_JOIN) {
    if (nwin == WINDOW)
      pass_oldest();
    win[nwin++] = *m;
    return;
  }
  if (m->op >= OP_FUNC_BEGIN) {
    /* A barrier: never optimize across it. */
    if (m->op == OP_LABEL)
      before_label(m->a);
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
