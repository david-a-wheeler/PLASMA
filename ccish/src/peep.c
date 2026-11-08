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
 *   ANDI m ANDI n            -> ANDI (m & n)  two byte-ANDs merge: AND
 *                                          is associative, and both clear
 *                                          the high byte, so a (char)
 *                                          cast on an int (ANDI 255)
 *                                          before a mask (ANDI n) is just
 *                                          the mask (255 & n is n)
 *   LLB/LAB n  ANDI 255      -> LLB/LAB n    a byte load already
 *                                          zero-extends, so a full-byte
 *                                          ANDI after it is redundant
 *                                          (a word load keeps it)
 *   const  NEG / COMP        -> const      e.g. -5 is one constant
 *   CW v   CW v              -> CW v DUP     two equal WORD constants: a CW
 *                                          loads two operand bytes, so a stack
 *                                          DUP beats a second CW (4 cyc, 2B)
 *   const OP const           -> const      for ADD, SUB, MUL, DIV, MOD,
 *                                          AND, IOR, XOR, SHL, SHR and the
 *                                          six comparisons: a backstop
 *                                          for the parser's folding,
 *                                          for the leftovers the peep's
 *                                          own rewrites make. Not for a
 *                                          zero divisor or a shift by
 *                                          sixteen or more, which are
 *                                          undefined in C (and the VMs
 *                                          disagree on the first)
 *   CN 0   ADD / SUB         -> (nothing)
 *   CN 0  IOR/XOR/SHL/SHR    -> (nothing)    x | 0, x ^ 0, x << 0, x >> 0
 *                                          and ~~x (COMP COMP) are no-ops,
 *                                          so both ops go
 *   CN 1   ADD / SUB         -> INCR / DECR   (also MINUS1 SUB, ...)
 *   const  ADD / SUB         -> ADDI / SUBI n  for adding n from 2 to 255
 *                                          either way (x - 5, x + -5:
 *                                          SUBI 5)
 *   ADDI/SUBI/INCR/DECR pair -> one op      two small constant adds/subs
 *                                          merge to their sum (x + 5 - 3:
 *                                          ADDI 5 SUBI 3 -> ADDI 2; the
 *                                          front end can't fold it, since
 *                                          x + 5 is not a constant)
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
 *   DROP  CN 0               -> ANDI 0     one op pops it and pushes 0
 *   const MUL, const = 2^n   -> CN n SHL   for n from 2 to 15
 *   DUP  ADD  ADD            -> IDXW       p + 2*i
 *   DUP  update  store  DROP -> update  store   x++ whose old value is
 *        unused. update is INCR, DECR, NEG, COMP, ADDI, SUBI, or a
 *        constant then ADD or SUB; store is SLW, SLB, SAW or SAB n, or SW
 *        or SB after an address computed only on the stack (LLW p, LLA a
 *        IDXLW i, LA g IDXAW i, ...).
 *   expr  DROP               -> (nothing)  for an expression that
 *                                          reads no memory but the frame
 *                                          (constants, LLW, LLB, LLA, LA,
 *                                          DUP, arithmetic: no LW, LAB,
 *                                          ADDAW, ..., which might read
 *                                          I/O)
 * And at a LEAVE or RET (dead_stores()), the stores to locals just
 * before it, with nothing between but ops on the stack, are dead (the
 * frame goes), unless a later op loads the local back: SLW or SLB n ->
 * DROP, DLW n -> (nothing), DLB n -> ANDI 255 (the byte it keeps); then
 * an expression and that DROP -> (nothing), as above. A loop's exit then a return
 * (SLW i DROP MINUS1 LEAVE) needn't store i.
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
#include "objfmt.h"
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
   * MINUS1, which are shorter than LAB's address word, nor for an
   * object that may move to the heap (FIX_HEAP), whose LA ccrel may
   * turn into an LAW. */
  if (absolute_op(b->op) >= 0 && a->fix != FIX_HEAP
      && (a->op == OP_CW || a->op == OP_CB || a->op == OP_CFFB
          || a->op == OP_LA)) {
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
  if (b->op == OP_AND && mop_const_value(a, &v) && !(v & 0xFF00)) {
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

/* Two consecutive byte-ANDs merge into one (ANDI m ANDI n becomes
 * ANDI (m & n)): AND is associative, and both clear the high byte, so
 * the earlier one is subsumed by the combined mask. A (char) cast on
 * an int (ANDI 255) meeting a small & constant (ANDI n) is the case
 * this folds, since 255 & n is n. It is pure stack arithmetic (no read
 * is added or removed), so it is safe even for a volatile value. */
static int rewrite_andi(Mop *a, Mop *b)
{
  if (a->op == OP_ANDI && b->op == OP_ANDI) {
    a->a &= b->a;
    nwin--;
    return 1;
  }
  return 0;
}

/* If m adds a small constant to the top of the stack (INCR, DECR, ADDI
 * or SUBI), set *v to that constant (positive for an add) and return 1;
 * 0 otherwise. INCR and DECR are the +/-1 forms of ADDI and SUBI. */
static int add_value(Mop *m, int *v)
{
  switch (m->op) {
  case OP_INCR: *v = 1; return 1;
  case OP_DECR: *v = -1; return 1;
  case OP_ADDI: *v = m->a; return 1;
  case OP_SUBI: *v = -m->a; return 1;
  }
  return 0;
}

/* Two small additive updates merge into one: the front end emits each
 * constant add or subtract as its own op (x + 5 - 3 is ADDI 5 SUBI 3,
 * since x + 5 is not a constant), but their sum is a constant, so one
 * op suffices. The sum is 0 -> drop both, +/-1 -> INCR / DECR,
 * +/-2 to +/-255 -> ADDI / SUBI; a larger magnitude stays as two ops.
 * It is pure stack arithmetic, safe even for a volatile value. */
static int rewrite_addsub(Mop *a, Mop *b)
{
  int x, y, t;
  if (!add_value(a, &x) || !add_value(b, &y))
    return 0;
  t = x + y;
  if (t < -255 || t > 255)
    return 0;
  if (t == 0) {
    nwin -= 2;
    return 1;
  }
  if (t > 0) {
    a->op = t == 1 ? OP_INCR : OP_ADDI;
  } else {
    a->op = t == -1 ? OP_DECR : OP_SUBI;
    t = -t;
  }
  a->a = t;
  nwin--;
  return 1;
}

/* A constant 0 under a no-op op (x | 0, x ^ 0, x << 0, x >> 0), or a
 * double complement (~~x), leaves the value unchanged, so both ops go.
 * The front end already folds x + 0 and x - 0; these are the rest.
 * Pure stack arithmetic, safe even for a volatile value. */
static int rewrite_noop(Mop *a, Mop *b)
{
  int v;
  if (mop_const_value(a, &v) && v == 0
      && (b->op == OP_IOR || b->op == OP_XOR || b->op == OP_SHL
          || b->op == OP_SHR)) {
    nwin -= 2;
    return 1;
  }
  if (a->op == OP_COMP && b->op == OP_COMP) {
    nwin -= 2;
    return 1;
  }
  return 0;
}

/* A byte load (LLB, LAB) already zero-extends, so a full-byte ANDI 255
 * right after it is redundant and goes. Not a word load (LLW, LAW),
 * whose high byte may be set. Pure stack, safe even for a volatile.
 * Feeds on the const AND -> ANDI peep (c & 255 becomes LLB ANDI 255).
 */
static int rewrite_load_andi(Mop *a, Mop *b)
{
  if (b->op == OP_ANDI && b->a == 0xFF
      && (a->op == OP_LLB || a->op == OP_LAB)) {
    nwin--;
    return 1;
  }
  return 0;
}

/* DROP CN 0 (discard a value, then push 0) is one ANDI 0: it pops the
 * value and pushes 0 in one op instead of two. The value is still
 * computed, for its side effects; only the drop-and-replace is cheaper.
 * Same two bytes, one fewer instruction. Pure stack, safe even for a
 * volatile value. */
static int rewrite_drop_zero(Mop *a, Mop *b)
{
  int v;
  if (a->op == OP_DROP && mop_const_value(b, &v) && v == 0) {
    a->op = OP_ANDI;
    a->a = 0;
    nwin--;
    return 1;
  }
  return 0;
}

/* Two equal WORD (CW) constants in a row push the same value twice; the
 * second is a DUP. A CW loads two operand bytes, so the stack copy (DUP,
 * 44 cycles) is cheaper than a second CW (48) by 4 cycles and 2 bytes. The
 * cheaper forms (MINUS1, CN, CB, CFFB) load from the opcode or one operand
 * byte (36-43 cycles), where a DUP is a cycle regression or a wash, so they
 * stay as two constants. Pure stack, safe even for a volatile value. */
static int rewrite_dup_const(Mop *a, Mop *b)
{
  int va, vb;
  if (a->op == OP_CW && b->op == OP_CW
      && mop_const_value(a, &va) && mop_const_value(b, &vb)
      && va == vb) {
    b->op = OP_DUP;
    b->a = 0;
    return 1;
  }
  return 0;
}

/* The 16-bit pattern in v as a signed int, as parse.c's wrap16: the
 * unsigned arithmetic works whether the host's int has 16 or 32 bits. */
static int signed_word(int v)
{
  unsigned u = (unsigned)v & 0xFFFF;
  if (u & 0x8000)
    return -(int)(0xFFFF - u) - 1;
  return (int)u;
}

/* c shifted right by y (0 to 15) as a signed 16-bit word, rounding
 * toward minus infinity: the sign bit shifts in, as on every VM. The
 * count is small, so a shift at a time is the whole op. */
static int shr_word(int c, int y)
{
  unsigned u = (unsigned)c & 0xFFFF;
  while (y-- > 0)
    u = (u >> 1) | ((u & 1) != 0 ? 0x8000 : 0);
  return signed_word((int)u);
}

/* If op folds two constants, c on the stack below a, set *v to the
 * 16-bit value the VM's op would push and return 1; 0 if the peep
 * must not fold it. The values are as signed words where the op is
 * signed (the VM's words are two's complement): division and modulo
 * are C's (toward zero), the comparisons give the VM's $FFFF / 0. The
 * no cases are C's undefined ones: a zero divisor, where the VMs
 * disagree, and a shift by sixteen or more. */
static int fold_pair(int op, int c, int a, int *v)
{
  int x = signed_word(c);
  int y = signed_word(a);
  int r;
  switch (op) {
  case OP_ADD:    r = x + y; break;
  case OP_SUB:    r = x - y; break;
  case OP_MUL:    r = x * y; break;
  case OP_DIV:    if (y == 0) return 0; r = x / y; break;
  case OP_MOD:    if (y == 0) return 0; r = x % y; break;
  case OP_AND:    r = c & a; break;
  case OP_IOR:    r = c | a; break;
  case OP_XOR:    r = c ^ a; break;
  case OP_SHL:
    if (y < 0 || y > 15) return 0;
    r = (int)(((unsigned)c & 0xFFFF) << y); break;
  case OP_SHR:    if (y < 0 || y > 15) return 0; r = shr_word(c, y); break;
  case OP_ISEQ:   r = c == a ? 0xFFFF : 0; break;
  case OP_ISNE:   r = c != a ? 0xFFFF : 0; break;
  case OP_ISGT:   r = x > y ? 0xFFFF : 0; break;
  case OP_ISLT:   r = x < y ? 0xFFFF : 0; break;
  case OP_ISGE:   r = x >= y ? 0xFFFF : 0; break;
  case OP_ISLE:   r = x <= y ? 0xFFFF : 0; break;
  default:        return 0;
  }
  *v = r & 0xFFFF;
  return 1;
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
  /* NOT ANDI n BRFLS / BRTRU -> BRTRU / BRFLS: branch on !x by testing x
   * with the opposite branch. The ANDI n (n non-zero) only normalizes the
   * truth value to 1 (PLASMA's NOT gives $FFFF), and a test branch cares
   * only about zero vs non-zero, so the mask is dropped. (Eliding it before
   * a non-branch truth use is NOT done: a following NOT belongs to another
   * !expr and would be clobbered.) */
  if (nwin >= 3 && win[nwin - 3].op == OP_NOT && a->op == OP_ANDI
      && a->a != 0 && is_test_branch(b->op)) {
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

/* Does window op m push a value, reading no memory but the frame (so
 * that a DROP of it can go with it)? */
static int frame_push(Mop *m)
{
  int v;
  return mop_const_value(m, &v) || m->op == OP_LLW || m->op == OP_LLB
         || m->op == OP_LLA || m->op == OP_LA || m->op == OP_DUP;
}

static void remove_op(int i);

/* Does window op m read a local: LLW, LLB, or an add of one? */
static int reads_local(Mop *m)
{
  return m->op == OP_LLW || m->op == OP_LLB || m->op == OP_ADDLB
         || m->op == OP_ADDLW || m->op == OP_IDXLB || m->op == OP_IDXLW;
}

/* The number of ops in the expression that ends just before window
 * index end, if it pushes one value and reads no memory but the frame
 * (frame_push(), or arithmetic: no load through an address or from an
 * absolute one, which might read I/O); else 0. */
static int frame_expr(int end)
{
  int n = expr_len(end), i;
  for (i = end - n; i < end; i++)
    if (!frame_push(&win[i]) && !(win[i].op >= OP_ADD && win[i].op <= OP_IDXW)
        && win[i].op != OP_ADDI && win[i].op != OP_SUBI
        && win[i].op != OP_ANDI && !reads_local(&win[i]))
      return 0;
  return n;
}

/* Remove the DROP at window index i and, if the expression before it
 * has no effect (frame_expr()), that too. Returns how many ops before i
 * went. */
static int drop_expr(int i)
{
  int n = frame_expr(i), k;
  if (n == 0)
    return 0;
  for (k = 0; k <= n; k++)
    remove_op(i - n);
  return n;
}


/* The window's last op is a LEAVE or RET: remove the stores to locals
 * before it that nothing reads back (the patterns at the top). It scans
 * back over ops that only work on the stack (stack_effect(), but no
 * load through an address, which might read a local through a pointer),
 * DROPs, loads of locals, and the stores, and stops at anything else. */
static void dead_stores(void)
{
  int i, j, pops, pushes, nread = 0, read[WINDOW];
  Mop *m;
  for (i = nwin - 2; i >= 0; i--) {
    m = &win[i];
    if (m->op == OP_SLW || m->op == OP_SLB || m->op == OP_DLW
        || m->op == OP_DLB) {
      for (j = 0; j < nread; j++)
        if (m->a - read[j] <= 1 && read[j] - m->a <= 1)
          break;
      if (j < nread)
        continue;               /* loaded back: not dead */
      if (m->op == OP_DLW) {
        remove_op(i);
      } else if (m->op == OP_DLB) {
        m->op = OP_ANDI;
        m->a = 0xFF;
      } else {
        m->op = OP_DROP;
        m->a = 0;
        i -= drop_expr(i);      /* and the value, if it has no effect */
      }
    } else if (reads_local(m)) {
      read[nread++] = m->a;
    } else if (m->op != OP_DROP
               && (!stack_effect(m, &pops, &pushes) || m->op == OP_LW
                   || m->op == OP_LB)) {
      break;
    }
  }
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
  /* const OP const -> const (fold_pair()): both sides constant is a
   * backstop for the parser's own folding. It comes first, so that
   * the one-constant rewrites below don't spend one constant into an
   * ADDI or the like, leaving the other stranded. */
  if (nwin >= 3) {
    int v1, v2, v;
    if (mop_const_value(&win[nwin - 3], &v1)
        && mop_const_value(a, &v2) && fold_pair(b->op, v1, v2, &v)) {
      Mop r;
      mop_const(&r, v);
      win[nwin - 3] = r;
      nwin -= 2;
      return 1;
    }
  }
  if (rewrite_load_store(a, b) || rewrite_const(a, b)
      || rewrite_branch(a, b) || rewrite_mul(a, b)
      || rewrite_andi(a, b) || rewrite_addsub(a, b)
      || rewrite_noop(a, b) || rewrite_load_andi(a, b)
      || rewrite_drop_zero(a, b) || rewrite_dup_const(a, b))
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
  /* expr DROP -> nothing, for an expression that reads no memory but
   * the frame. */
  if (b->op == OP_DROP && drop_expr(nwin - 1))
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
  if (m->op == OP_LEAVE || m->op == OP_RET)
    dead_stores();
  while (rewrite())
    ;
}
