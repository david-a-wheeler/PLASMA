// Stage 3: peephole optimizer. Holds a small window of machine ops,
// rewrites patterns at the end of the window, and passes ops on to the
// emitter.
//
// Dead code: after an unconditional exit (LEAVE, RET, BRNCH, SEL),
// nothing can run until the next barrier, so ops are discarded until
// then. Barriers are the pseudo-ops: function start and end, labels (a
// label is where a branch can land, which makes code reachable again),
// and a SEL's case table (the code after it runs when no case matches).
// This removes the implicit "return 0" the parser adds after a final
// return statement, the jump after an if-part that ends in return, and
// the bodies of if (0) and while (0).
//
// Patterns (each checked at the end of the window after every op):
//   DLW n  DROP              -> SLW n      assignment whose value is unused
//   SLW n  LLW n             -> DLW n      store, then load the same local
//                                          (these two also for bytes, and
//                                          for globals: DAW, SAW, LAW, ...)
//   CW a   LB / LW / SB / SW -> LAB / LAW / SAB / SAW a   constant address
//                                          (also after CB, CFFB, or LA)
//   const  AND               -> ANDI const for a constant from 0 to 255
//   const  IOR               -> ORI const  for a constant from 1 to 255
//   DROP  DROP               -> DROP2      one op, as the first leaves
//                                          the window (pass_oldest()), so
//                                          that every pattern sees each,
//                                          and any two that meet merge
//   ANDI m ANDI n            -> ANDI (m & n)  two byte-ANDs merge: AND
//                                          is associative, and both clear
//                                          the high byte, so a (char)
//                                          cast on an int (ANDI 255)
//                                          before a mask (ANDI n) is just
//                                          the mask (255 & n is n)
//   LLB/LAB n  ANDI 255      -> LLB/LAB n    a byte load already
//                                          zero-extends, so a full-byte
//                                          ANDI after it is redundant
//                                          (a word load keeps it)
//   const  NEG / COMP        -> const      e.g. -5 is one constant
//   CW v   CW v              -> CW v DUP     two equal WORD constants: a CW
//                                          loads two operand bytes, so a stack
//                                          DUP beats a second CW (4 cyc, 2B)
//   LLW n  LLW n  op         -> LLW n  DUP  op  a load repeated (also LLB,
//                                          LAW, LAB, or after a keeping
//                                          store, DLW n LLW n): DUP is
//                                          10-28 cycles faster. Not for a
//                                          volatile, and only once op is
//                                          there and no rule joined it to
//                                          the load (LLW n ADD -> ADDLW n
//                                          is better)
//   const OP const           -> const      for ADD, SUB, MUL, DIV, MOD,
//                                          AND, IOR, XOR, SHL, SHR and the
//                                          six comparisons: a backstop
//                                          for the parser's folding,
//                                          for the leftovers the peep's
//                                          own rewrites make. Not for a
//                                          zero divisor or a shift by
//                                          sixteen or more, which are
//                                          undefined in C (and the VMs
//                                          disagree on the first)
//   CN 0   ADD / SUB         -> (nothing)
//   CN 0  IOR/XOR/SHL/SHR    -> (nothing)    x | 0, x ^ 0, x << 0, x >> 0
//                                          and ~~x (COMP COMP) are no-ops,
//                                          so both ops go
//   CN 1  DIV                -> (nothing)    x / 1 is x (x * 1: below)
//   CN 1   ADD / SUB         -> INCR / DECR   (also MINUS1 SUB, ...)
//   const  ADD / SUB         -> ADDI / SUBI n  for adding n from 2 to 255
//                                          either way (x - 5, x + -5:
//                                          SUBI 5)
//   ADDI/SUBI/INCR/DECR pair -> one op      two small constant adds/subs
//                                          merge to their sum (x + 5 - 3:
//                                          ADDI 5 SUBI 3 -> ADDI 2; the
//                                          front end can't fold it, since
//                                          x + 5 is not a constant)
//   LLW n  ADD / IDXW        -> ADDLW / IDXLW n  (also LLB, LAW, LAB:
//                                          ADDLB, IDXLB, ADDAW, ...)
//   NEG  BRFLS / BRTRU       -> BRFLS / BRTRU    comparison used as a test
//   NOT ANDI 1  BRFLS/BRTRU  -> BRTRU / BRFLS    branch on !x
//   NOT  BRFLS / BRTRU       -> BRTRU / BRFLS    the same, without the
//                                          ANDI (from x == 0, below)
//   NOT  ANDI n  NOT         -> NOT NOT    !!x: NOT tests only zero
//                                          against non-zero
//   ISEQ / ISNE  BRFLS/BRTRU -> BREQ / BRNE
//   CN 0  BREQ / BRNE        -> BRFLS / BRTRU    x == 0 is a test of x
//   CN 2  MUL                -> DUP ADD    (faster than CN 1 SHL; see rewrite)
//   CN 3  MUL                -> DUP IDXW
//   CN 0  MUL                -> DROP CN 0
//   CN 1  MOD                -> DROP CN 0    x % 1 is 0 for any int
//   MINUS1  MUL / DIV / XOR  -> NEG / NEG / COMP   x * -1, x / -1, x ^ -1
//   MINUS1  AND              -> (nothing)  x & -1 is x
//   MINUS1  MOD / IOR        -> DROP CN 0 / DROP MINUS1
//                                          x % -1 is 0, x | -1 is -1;
//                                          and x goes too if it has no
//                                          effect
//   DUP  SUB / XOR           -> DROP CN 0  x - x, x ^ x (and x goes if it
//                                          has no effect); never two
//                                          volatile loads, only a DUP
//   DUP  AND / IOR           -> (nothing)  x & x, x | x
//   CN 0  expr  SUB          -> expr NEG   0 - x, for an expression with
//                                          no effect
//   CN 0  expr  SHL/SHR/DIV/MOD -> CN 0  the same: 0 << x, 0 >> x,
//                                          0 / x and 0 % x
//   const m  SHL  const n  SHL -> const m+n  SHL   two shifts (also SHR),
//                                          a count of 16 or more making
//                                          SHL's result 0 and SHR's
//                                          count 15
//   CN 1  SHL                -> DUP ADD    x << 1, as for x * 2
//   CN 0  ISEQ               -> NOT        x == 0: PLASMA's NOT gives
//                                          ISEQ's true ($FFFF) and false
//   NEG  NEG                 -> (nothing)  -(-x) is x, -32768 too
//   LLA n / LA a  add c      -> LLA n+c / LA a+c   an address plus a
//                                          constant (INCR ... SUBI c) is
//                                          one address, on the terms of
//                                          a[i + c] below
//   CN 1  MUL                -> (nothing)
//   DROP  CN 0               -> ANDI 0     one op pops it and pushes 0
//   expr  ANDI 0             -> CN 0       for an expression with no
//                                          effect: x & 0, x * 0, x % 1
//   const MUL, const = 2^n   -> CN n SHL   for n from 2 to 15
//   DUP  ADD  ADD            -> IDXW       p + 2*i
//   LA a  expr  add c  ADD   -> LA a+c  expr  ADD   a[i + c] (add c is
//        INCR, DECR, ADDI or SUBI c, expr any index with no effect): the
//        constant goes into the array's address, twice it before IDXW
//        (an int's a[i + c]), so the index is one load and ADD fuses
//        (LA a+c ADDLW i: 2 bytes and 2 ops less). For a data or string
//        address (ccrel wraps an offset outside the object, which the
//        index brings back), and for LLA n while n+c is a frame offset;
//        not an extern's, which ccrel may move to the heap.
//   DUP  update  store  DROP -> update  store   x++ whose old value is
//        unused, or (with no update) an assignment's. update is INCR,
//        DECR, NEG, COMP, ADDI, SUBI, or a constant then ADD or SUB;
//        store is SLW, SLB, SAW or SAB n, or SW or SB after an address
//        computed only on the stack (LLW p, LLA a IDXLW i, LA g IDXAW
//        i, ...), volatile or not (the store is still made).
//   expr  DROP               -> (nothing)  for an expression with no
//                                          effect: constants, plain loads
//                                          (LLW, LAW, LW, ...), DUP, and
//                                          arithmetic; not a call, a
//                                          store, or a volatile load,
//                                          which must happen (I/O is
//                                          volatile)
// And at a LEAVE or RET (dead_stores()), the stores to locals just
// before it, with nothing between but ops on the stack, are dead (the
// frame goes), unless a later op loads the local back: SLW or SLB n ->
// DROP, DLW n -> (nothing), DLB n -> ANDI 255 (the byte it keeps); then
// an expression and that DROP -> (nothing), as above. A loop's exit then a return
// (SLW i DROP MINUS1 LEAVE) needn't store i.
// And at a label L, just before it flushes the window (skipping joins):
//   BRNCH L                  -> (nothing)  a jump to the very next op
//   Bcond L  BRNCH T         -> Binv T     the opposite branch (BRFLS /
//                                          BRTRU, BREQ / BRNE); for
//                                          a && b || c, !(a && b), ...
// A join (OP_JOIN, from && and ||) emits no code: it isn't a barrier,
// but waits in the window in order, even in dead code.
// All are valid for C: they don't change any value the program sees.
#include "build.h"
#include "cclimits.h"
#include "obj.h"
#include "obj.fmt.h"
#include "mop.h"
#include "lower.h"
#include "peep.h"
#include "emit.h"

// Big enough for an unused a[i]++ through a pointer with an offset:
// DUP INCR LLW p LLW i IDXW CB d ADD SW DROP.
#define WINDOW 10

static Mop win[WINDOW];
static int nwin;
static int dead;            // code is unreachable until the next barrier

// Sets of opcodes, as bit tests rather than an == for each member:
// PLASMA's opcodes are even, and related ones sit together, so a set
// is one or two tests. ((op - B) & ~6) == 0 is the 4 even ops from B;
// (op & ~2) == B is B and B + 2 (bit 1 of B clear). An odd op, a
// volatile form or a pseudo-op, is in none, as with ==. After each,
// one _Static_assert shows it holds every member, and another that it
// holds none of the ops it's likeliest to catch by mistake: the ops
// just outside it, the ones its mask would let through if it were
// wrong, its members' volatile forms, and close relatives that don't
// belong. A two-test macro names op twice, so pass it a local, not
// b->op: C has no way to bind a value inside an expression, and these
// sets have no one test that picks out just their ops.

// Does op always transfer control (BRNCH, SEL, LEAVE, RET), so that
// what follows can't run until the next label?
#define IS_EXIT(op) (((op) & ~2) == OP_BRNCH \
                     || (((op) - OP_LEAVE) & ~2) == 0)
_Static_assert(IS_EXIT(OP_BRNCH) && IS_EXIT(OP_SEL) && IS_EXIT(OP_LEAVE)
               && IS_EXIT(OP_RET), "IS_EXIT misses a member");
_Static_assert(!IS_EXIT(OP_BRTRU) && !IS_EXIT(OP_BRFLS)
               && !IS_EXIT(OP_CALL) && !IS_EXIT(OP_ICAL)
               && !IS_EXIT(OP_ENTER) && !IS_EXIT(OP_CFFB)
               && !IS_EXIT(OP_BREQ) && !IS_EXIT(OP_BRNE)
               && !IS_EXIT(OP_BRGT) && !IS_EXIT(OP_BRLT)
               && !IS_EXIT(OP_BRNCH | MOP_VOLATILE)
               && !IS_EXIT(OP_LEAVE | MOP_VOLATILE),
               "IS_EXIT holds a non-member");

// Is op a one-operand update that can sit between DUP and a store in
// x++ (INCR, DECR, NEG, COMP, ADDI, SUBI)?
#define IS_UPDATE(op) ((((op) - OP_INCR) & ~6) == 0 \
                       || ((op) & ~2) == OP_ADDI)
_Static_assert(IS_UPDATE(OP_INCR) && IS_UPDATE(OP_DECR)
               && IS_UPDATE(OP_NEG) && IS_UPDATE(OP_COMP)
               && IS_UPDATE(OP_ADDI) && IS_UPDATE(OP_SUBI),
               "IS_UPDATE misses a member");
_Static_assert(!IS_UPDATE(OP_MOD) && !IS_UPDATE(OP_AND)
               && !IS_UPDATE(OP_DUP) && !IS_UPDATE(OP_ANDI)
               && !IS_UPDATE(OP_ORI) && !IS_UPDATE(OP_NOT)
               && !IS_UPDATE(OP_ADD) && !IS_UPDATE(OP_SUB)
               && !IS_UPDATE(OP_INCR | MOP_VOLATILE)
               && !IS_UPDATE(OP_ADDI | MOP_VOLATILE),
               "IS_UPDATE holds a non-member");

// Is op a store that takes its address as an operand (SLB, SLW, SAB,
// SAW)?
#define IS_OPERAND_STORE(op) ((((op) - OP_SLB) & ~6) == 0)
_Static_assert(IS_OPERAND_STORE(OP_SLB) && IS_OPERAND_STORE(OP_SLW)
               && IS_OPERAND_STORE(OP_SAB) && IS_OPERAND_STORE(OP_SAW),
               "IS_OPERAND_STORE misses a member");
_Static_assert(!IS_OPERAND_STORE(OP_SB) && !IS_OPERAND_STORE(OP_SW)
               && !IS_OPERAND_STORE(OP_DAB) && !IS_OPERAND_STORE(OP_DAW)
               && !IS_OPERAND_STORE(OP_DLB) && !IS_OPERAND_STORE(OP_DLW)
               && !IS_OPERAND_STORE(OP_LLW) && !IS_OPERAND_STORE(OP_LAW)
               && !IS_OPERAND_STORE(OP_SLB | MOP_VOLATILE),
               "IS_OPERAND_STORE holds a non-member");

// Does op read a local: LLB, LLW, or an add of one (ADDLB, ADDLW,
// IDXLB, IDXLW)?
#define READS_LOCAL(op) (((op) & ~2) == OP_LLB \
                         || ((op) & ~0x0A) == OP_ADDLB)
_Static_assert(READS_LOCAL(OP_LLB) && READS_LOCAL(OP_LLW)
               && READS_LOCAL(OP_ADDLB) && READS_LOCAL(OP_ADDLW)
               && READS_LOCAL(OP_IDXLB) && READS_LOCAL(OP_IDXLW),
               "READS_LOCAL misses a member");
_Static_assert(!READS_LOCAL(OP_LB) && !READS_LOCAL(OP_LW)
               && !READS_LOCAL(OP_LAB) && !READS_LOCAL(OP_LAW)
               && !READS_LOCAL(OP_LLA) && !READS_LOCAL(OP_DLB)
               && !READS_LOCAL(OP_ADDAB) && !READS_LOCAL(OP_ADDAW)
               && !READS_LOCAL(OP_IDXAB) && !READS_LOCAL(OP_IDXAW)
               && !READS_LOCAL(OP_SUBBRGE) && !READS_LOCAL(OP_IDXW)
               && !READS_LOCAL(OP_LLW | MOP_VOLATILE)
               && !READS_LOCAL(OP_ADDLW | MOP_VOLATILE),
               "READS_LOCAL holds a non-member");

// Is op a store to a local (DLB, DLW, SLB, SLW)? From DLB, they're 0,
// 2, 8 and 10: the values with only bits 1 and 3 set.
#define STORES_LOCAL(op) ((((op) - OP_DLB) & ~0x0A) == 0)
_Static_assert(STORES_LOCAL(OP_DLB) && STORES_LOCAL(OP_DLW)
               && STORES_LOCAL(OP_SLB) && STORES_LOCAL(OP_SLW),
               "STORES_LOCAL misses a member");
_Static_assert(!STORES_LOCAL(OP_SB) && !STORES_LOCAL(OP_SW)
               && !STORES_LOCAL(OP_LAW) && !STORES_LOCAL(OP_LLW)
               && !STORES_LOCAL(OP_SAB) && !STORES_LOCAL(OP_SAW)
               && !STORES_LOCAL(OP_DAB) && !STORES_LOCAL(OP_DAW)
               && !STORES_LOCAL(OP_DLB | MOP_VOLATILE)
               && !STORES_LOCAL(OP_SLW | MOP_VOLATILE),
               "STORES_LOCAL holds a non-member");

// Is op one whose 0 on the right leaves the value as it was (IOR, XOR,
// SHL, SHR)?
#define IS_ZERO_IDENTITY(op) ((((op) - OP_IOR) & ~6) == 0)
_Static_assert(IS_ZERO_IDENTITY(OP_IOR) && IS_ZERO_IDENTITY(OP_XOR)
               && IS_ZERO_IDENTITY(OP_SHL) && IS_ZERO_IDENTITY(OP_SHR),
               "IS_ZERO_IDENTITY misses a member");
_Static_assert(!IS_ZERO_IDENTITY(OP_AND) && !IS_ZERO_IDENTITY(OP_IDXW)
               && !IS_ZERO_IDENTITY(OP_ADD) && !IS_ZERO_IDENTITY(OP_SUB)
               && !IS_ZERO_IDENTITY(OP_MUL) && !IS_ZERO_IDENTITY(OP_DIV)
               && !IS_ZERO_IDENTITY(OP_COMP)
               && !IS_ZERO_IDENTITY(OP_IOR | MOP_VOLATILE),
               "IS_ZERO_IDENTITY holds a non-member");

// Is op one whose 0 on the left gives 0, or -x for SUB (SUB, DIV, MOD,
// SHL, SHR)?
#define IS_ZERO_LEFT(op) ((op) == OP_SUB || ((op) & ~2) == OP_DIV \
                          || (((op) - OP_SHL) & ~2) == 0)
_Static_assert(IS_ZERO_LEFT(OP_SUB) && IS_ZERO_LEFT(OP_DIV)
               && IS_ZERO_LEFT(OP_MOD) && IS_ZERO_LEFT(OP_SHL)
               && IS_ZERO_LEFT(OP_SHR), "IS_ZERO_LEFT misses a member");
_Static_assert(!IS_ZERO_LEFT(OP_ADD) && !IS_ZERO_LEFT(OP_MUL)
               && !IS_ZERO_LEFT(OP_INCR) && !IS_ZERO_LEFT(OP_XOR)
               && !IS_ZERO_LEFT(OP_IDXW) && !IS_ZERO_LEFT(OP_AND)
               && !IS_ZERO_LEFT(OP_IOR) && !IS_ZERO_LEFT(OP_NOT)
               && !IS_ZERO_LEFT(OP_SUB | MOP_VOLATILE)
               && !IS_ZERO_LEFT(OP_DIV | MOP_VOLATILE)
               && !IS_ZERO_LEFT(OP_SHL | MOP_VOLATILE),
               "IS_ZERO_LEFT holds a non-member");

// The guards rewrite() puts before its rules, so that a pass where
// none can match calls none of them: most rules need one op, or a
// constant, as a or b. A guard holds at least everything its rule
// matches, and the rules keep their order, so the code doesn't change.

// Is op a constant add, as add_value() takes (INCR, DECR, ADDI, SUBI)?
#define IS_ADD_VALUE(op) ((((op) - OP_INCR) & ~2) == 0 \
                          || ((op) & ~2) == OP_ADDI)
_Static_assert(IS_ADD_VALUE(OP_INCR) && IS_ADD_VALUE(OP_DECR)
               && IS_ADD_VALUE(OP_ADDI) && IS_ADD_VALUE(OP_SUBI),
               "IS_ADD_VALUE misses a member");
_Static_assert(!IS_ADD_VALUE(OP_MOD) && !IS_ADD_VALUE(OP_NEG)
               && !IS_ADD_VALUE(OP_DUP) && !IS_ADD_VALUE(OP_ANDI)
               && !IS_ADD_VALUE(OP_ADD) && !IS_ADD_VALUE(OP_SUB)
               && !IS_ADD_VALUE(OP_ADDLB) && !IS_ADD_VALUE(OP_ADDAW)
               && !IS_ADD_VALUE(OP_INCR | MOP_VOLATILE)
               && !IS_ADD_VALUE(OP_ADDI | MOP_VOLATILE),
               "IS_ADD_VALUE holds a non-member");

// Is op a branch that tests a value or compares two (BRFLS, BRTRU,
// BREQ, BRNE), as rewrite_branch()'s patterns end?
#define IS_VALUE_BRANCH(op) (((op) & ~2) == OP_BRFLS \
                             || (((op) - OP_BREQ) & ~2) == 0)
_Static_assert(IS_VALUE_BRANCH(OP_BRFLS) && IS_VALUE_BRANCH(OP_BRTRU)
               && IS_VALUE_BRANCH(OP_BREQ) && IS_VALUE_BRANCH(OP_BRNE),
               "IS_VALUE_BRANCH misses a member");
_Static_assert(!IS_VALUE_BRANCH(OP_BRNCH) && !IS_VALUE_BRANCH(OP_SEL)
               && !IS_VALUE_BRANCH(OP_ISLE) && !IS_VALUE_BRANCH(OP_MINUS1)
               && !IS_VALUE_BRANCH(OP_LA) && !IS_VALUE_BRANCH(OP_BRGT)
               && !IS_VALUE_BRANCH(OP_BRLT)
               && !IS_VALUE_BRANCH(OP_BRFLS | MOP_VOLATILE)
               && !IS_VALUE_BRANCH(OP_BREQ | MOP_VOLATILE),
               "IS_VALUE_BRANCH holds a non-member");
// Is op NEG or COMP: a pair, an even number and the next.
#define IS_NEG_COMP(op)  (((op) - OP_NEG) & ~2) == 0
_Static_assert(IS_NEG_COMP(OP_NEG) && IS_NEG_COMP(OP_COMP)
               && OP_COMP - OP_NEG == 2
               && !IS_NEG_COMP(OP_MUL) && !IS_NEG_COMP(OP_SUB)
               && !IS_NEG_COMP(OP_NEG | MOP_VOLATILE),
               "IS_NEG_COMP isn't NEG and COMP, a pair");
// The frame add of a load (LOAD_FRAME_ADD(LLW, 0) is ADDLW): the VM
// numbers the adds in the loads' order, ADDLB at LLB + $4C, and the
// IDX adds at + 8 of their plain adds (asserted below).
#define LOAD_FRAME_ADD(load, idx) ((load) + (OP_ADDLB - OP_LLB) \
                                   + ((idx) ? (OP_IDXLB - OP_ADDLB) : 0))
_Static_assert(OP_ADDLB - OP_LLB == 0x4C && OP_IDXLB - OP_ADDLB == 8
               && OP_ADDLW - OP_LLW == 0x4C && OP_IDXLW - OP_ADDLW == 8
               && OP_ADDAB - OP_LAB == 0x4C && OP_IDXAB - OP_ADDAB == 8
               && OP_ADDAW - OP_LAW == 0x4C && OP_IDXAW - OP_ADDAW == 8,
               "the frame adds aren't in the loads' order, ADDLB at"
               " LLB + $4C, the IDX adds at + 8");

// Send the oldest op in the window on to stage 4; or, if it's a DROP
// and so is the next, make that a DROP2 (one op, a byte less).
static void pass_oldest(void)
{
  int i;
  if (nwin > 1 && win[0].op == OP_DROP && win[1].op == OP_DROP)
    win[1].op = OP_DROP2;
  else
    emit_put(&win[0]);
  for (i = 1; i < nwin; i++)
    win[i - 1] = win[i];
  nwin--;
}

// Send every op in the window on to stage 4, oldest first.
void peep_flush(void)
{
  while (nwin > 0)
    pass_oldest();
}

// Is op a branch that pops a value and tests it for zero?
static int is_test_branch(int op)
{
  return op == OP_BRFLS || op == OP_BRTRU;
}

// The branch with the opposite test: BRFLS for BRTRU, BREQ for BRNE,
// and back; or -1 for any other op.
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

// The store that doesn't keep its value for a store that does (SLW for
// DLW, and so on for DLB, DAW and DAB), or -1. A volatile one opts in
// (it is still one store), and keeps its mark.
static int plain_store(int op)
{
  int v = op & MOP_VOLATILE;
  switch (MOP_PLAIN(op)) {
  case OP_DLW: return OP_SLW | v;
  case OP_DLB: return OP_SLB | v;
  case OP_DAW: return OP_SAW | v;
  case OP_DAB: return OP_SAB | v;
  }
  return -1;
}

// The store that keeps its value, if load reads back what store wrote
// (DLW for SLW then LLW, and so on for bytes and globals), or -1. The
// caller checks that the two have the same address.
static int keeping_store(int store, int load)
{
  if (store == OP_SLW && load == OP_LLW) return OP_DLW;
  if (store == OP_SLB && load == OP_LLB) return OP_DLB;
  if (store == OP_SAW && load == OP_LAW) return OP_DAW;
  if (store == OP_SAB && load == OP_LAB) return OP_DAB;
  return -1;
}

// The absolute-address form of a load or store through an address on
// the stack (LAB for LB, and so on), or -1. A volatile one opts in (it
// is still one access), and keeps its mark: *SPEAKER is one LAB.
static int absolute_op(int op)
{
  int v = op & MOP_VOLATILE;
  switch (MOP_PLAIN(op)) {
  case OP_LB: return OP_LAB | v;
  case OP_LW: return OP_LAW | v;
  case OP_SB: return OP_SAB | v;
  case OP_SW: return OP_SAW | v;
  }
  return -1;
}

// The op that adds a load's value (ADDLW for LLW, and so on for LLB,
// LAW and LAB), or that adds twice it if idx (IDXLW, ...), or -1. The
// VM numbers both sets of four in the loads' order: ADDLB is LLB +
// $4C, and IDXLB is ADDLB + 8.
static int fused_op(int load, int idx)
{
  int v = load & MOP_VOLATILE;  // a volatile load opts in: still one
  load = MOP_PLAIN(load);
  if (load < OP_LLB || load > OP_LAW)
    return -1;
  return LOAD_FRAME_ADD(load, idx) | v;
}

// Does op take one value and give one back, adding something to it
// (ADDI, SUBI, or ADDLB to IDXAW), with no other effect? Not a
// volatile fused load (odd), which drop_expr() mustn't delete.
static int is_add_op(int op)
{
  return op == OP_ADDI || op == OP_SUBI
         || (op >= OP_ADDLB && op <= OP_IDXAW && !MOP_IS_VOLATILE(op));
}

// If m is an op that only works on the stack (no branch, call, store,
// or volatile access; a plain load is one), set how many values it pops
// and pushes and return 1. Such an op can go when its value does: a
// plain load has no effect a C program may see.
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
  case OP_COMP: case OP_ANDI: case OP_ORI: case OP_NOT:
    *pops = 1;
    return 1;
  case OP_DUP:
    *pops = 1;
    *pushes = 2;
    return 1;
  }
  *pops = 2;                    // the rest of them take two
  return MOP_IS_ARITH(m->op);
}

// The number of ops in the self-contained expression that ends just
// before window index end and pushes one value (such as the address
// LLA a LLW i IDXW), or 0 if there isn't one. It scans backward,
// counting the values still needed.
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

// The number of ops in the store that ends just before window index
// end: SLW / SLB / SAW / SAB n, or an address expression then SW / SB,
// either of them volatile (only the copy of its value goes). 0 if
// there isn't one.
static int store_len(int end)
{
  int op = end >= 1 ? win[end - 1].op : -1, n;
  op = MOP_PLAIN(op);
  if (IS_OPERAND_STORE(op))
    return 1;
  if (op == OP_SW || op == OP_SB) {
    n = expr_len(end - 1);
    return n ? n + 1 : 0;
  }
  return 0;
}

// The number of ops (1 or 2) in the update that ends just before window
// index end: a unary op, or a constant then ADD / SUB. 0 if none.
static int update_len(int end)
{
  int v, op = end >= 1 ? win[end - 1].op : -1;
  if (IS_UPDATE(op))
    return 1;
  if (end >= 2 && (op == OP_ADD || op == OP_SUB)
      && mop_const_value(&win[end - 2], &v))
    return 2;
  return 0;
}

// With DROP at the end of the window, match DUP update store DROP (an
// x++ whose old value is unused), or DUP store DROP (an assignment
// whose value is unused, such as a volatile's, which cccomp keeps),
// and remove the DUP and the DROP. Returns 1 if it did.
static int drop_dup(void)
{
  int s = store_len(nwin - 1), u, d, i;
  if (s == 0)
    return 0;
  u = update_len(nwin - 1 - s);
  d = nwin - 2 - s - u;           // where the DUP must be
  if (d < 0 || win[d].op != OP_DUP)
    return 0;
  for (i = d; i < nwin - 2; i++)
    win[i] = win[i + 1];
  nwin -= 2;
  return 1;
}

static int fold_addr(Mop *base, Mop *m);

// The patterns on loads and stores, with a and b the last two ops in
// the window. Returns 1 if one changed anything.
static int rewrite_load_store(Mop *a, Mop *b)
{
  int v, aop = a->op, bop = b->op;

  // DLW n DROP -> SLW n (also DLB, DAW, DAB): an assignment whose
  // value is unused just stores, without keeping a copy to throw away.
  if (bop == OP_DROP && plain_store(aop) >= 0) {
    a->op = plain_store(a->op);
    nwin--;
    return 1;
  }
  // LLA n add c -> LLA n+c, and LA a add c -> LA a+c: one address, on
  // rewrite_index()'s terms.
  if ((aop == OP_LA || aop == OP_LLA) && IS_ADD_VALUE(bop)
      && fold_addr(a, b)) {
    nwin--;
    return 1;
  }
  // SLW n LLW n -> DLW n (also for bytes, and SAW / SAB with LAW / LAB):
  // storing a variable and then loading it again becomes a store that
  // keeps the value. DLB keeps the stored byte, which is what LLB would
  // load.
  if (IS_OPERAND_STORE(aop) && keeping_store(aop, bop) >= 0
      && a->a == b->a && a->fix == b->fix && a->fn == b->fn) {
    a->op = keeping_store(aop, bop);
    nwin--;
    return 1;
  }
  // CW addr LB -> LAB addr (also LW, SB, SW, after CB or CFFB, or LA
  // for a data address): a load or store through a constant address
  // is one op, such as reading the speaker at $C030. Not after CN or
  // MINUS1, which are shorter than LAB's address word, nor for an
  // object that may move to the heap (FIX_HEAP), whose LA ccrel may
  // turn into an LAW.
  if ((aop == OP_CW || aop == OP_CB || aop == OP_CFFB || aop == OP_LA)
      && a->fix != FIX_HEAP && absolute_op(bop) >= 0) {
    if (aop != OP_LA) {
      mop_const_value(a, &v);
      a->a = v;
    }
    a->op = absolute_op(bop);
    nwin--;
    return 1;
  }
  // LLW n ADD -> ADDLW n, and LLW n IDXW -> IDXLW n (also for LLB, LAW
  // and LAB): adding a variable's value, or twice it, is one op, a
  // byte smaller.
  if ((bop == OP_ADD || bop == OP_IDXW)
      && fused_op(aop, bop == OP_IDXW) >= 0) {
    a->op = fused_op(aop, bop == OP_IDXW);
    nwin--;
    return 1;
  }
  return 0;
}

static int add_value(Mop *m, int *v);

// If base is an address that can take an addend d (LA of data, the
// zero area or a string, which ccrel wraps, or of a global reached
// through its
// symbol, whose word holds the addend; LLA while it stays a frame
// offset; not a global that may move to the heap, FIX_HEAP, whose LA
// ccrel may rewrite), add it and return 1.
static int add_to_addr(Mop *base, int d)
{
  if (base->op == OP_LA ? base->fix != FIX_DATA && base->fix != FIX_STR
                          && base->fix != FIX_DSYM && base->fix != FIX_ZERO
      : base->op != OP_LLA || base->a + d < 0 || base->a + d > 255)
    return 0;
  base->a += d;
  return 1;
}

// LLA n or LA a (base), then a constant add (m): fold it, and return 1.
static int fold_addr(Mop *base, Mop *m)
{
  int v;
  return add_value(m, &v) && add_to_addr(base, v);
}

// With b an ADD or IDXW and a a constant add, the a[i + c] pattern
// (the list at the top): fold c into the array's address. Returns 1 if
// it did.
static int rewrite_index(Mop *a, Mop *b)
{
  int v, n, d;
  Mop *base;
  if ((b->op != OP_ADD && b->op != OP_IDXW) || !add_value(a, &v)
      || (n = expr_len(nwin - 2)) == 0 || nwin - 3 - n < 0)
    return 0;
  base = &win[nwin - 3 - n];
  d = b->op == OP_IDXW ? 2 * v : v;
  if (!add_to_addr(base, d))
    return 0;
  *a = *b;                      // the add goes
  nwin--;
  return 1;
}

// The patterns on a constant (a) and the op that uses it (b). Returns
// 1 if one changed anything.
static int rewrite_const(Mop *a, Mop *b)
{
  int v;

  // const AND -> ANDI const, for a constant from 0 to 255: one
  // two-byte op (it clears the high byte, as the AND would); and const
  // IOR -> ORI const, from 1 to 255 (x | 0 is rewrite_noop()'s).
  if ((b->op == OP_AND || b->op == OP_IOR) && mop_const_value(a, &v)
      && !(v & 0xFF00) && (v || b->op == OP_AND)) {
    a->op = MOP_BYTE_FORM(b->op);
    a->a = v;
    nwin--;
    return 1;
  }
  // CN 0 ISEQ -> NOT: x == 0 is !x, with ISEQ's own true ($FFFF).
  if (b->op == OP_ISEQ && mop_const_value(a, &v) && v == 0) {
    a->op = OP_NOT;
    nwin--;
    return 1;
  }
  // CN 1 SHL -> DUP ADD: x << 1, as rewrite_mul() does x * 2.
  if (b->op == OP_SHL && mop_const_value(a, &v) && v == 1) {
    a->op = OP_DUP;
    b->op = OP_ADD;
    return 1;
  }
  // const NEG / COMP -> const: negate or complement the constant at
  // compile time (a backstop for the parser's folding).
  if ((b->op == OP_NEG || b->op == OP_COMP) && mop_const_value(a, &v)) {
    mop_const(a, b->op == OP_NEG ? -v : ~v);
    nwin--;
    return 1;
  }
  // const ADD / SUB, adding d (the constant, or minus it for SUB):
  // d = 0 -> nothing (p[0] when p is itself loaded, as in
  // names[i][0]); 1 or -1 -> INCR or DECR; up to 255 either way ->
  // ADDI or SUBI (x - 5 and x + -5 are both SUBI 5). One op instead of
  // two: a byte smaller after CB or CFFB, and faster after CN.
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

// Two consecutive byte-ANDs merge into one (ANDI m ANDI n becomes
// ANDI (m & n)): AND is associative, and both clear the high byte, so
// the earlier one is subsumed by the combined mask. A (char) cast on
// an int (ANDI 255) meeting a small & constant (ANDI n) is the case
// this folds, since 255 & n is n. It is pure stack arithmetic (no read
// is added or removed), so it is safe even for a volatile value.
static int rewrite_andi(Mop *a, Mop *b)
{
  if (a->op == OP_ANDI && b->op == OP_ANDI) {
    a->a &= b->a;
    nwin--;
    return 1;
  }
  return 0;
}

// If m adds a small constant to the top of the stack (INCR, DECR, ADDI
// or SUBI), set *v to that constant (positive for an add) and return 1;
// 0 otherwise. INCR and DECR are the +/-1 forms of ADDI and SUBI.
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

// Two small additive updates merge into one: the front end emits each
// constant add or subtract as its own op (x + 5 - 3 is ADDI 5 SUBI 3,
// since x + 5 is not a constant), but their sum is a constant, so one
// op suffices. The sum is 0 -> drop both, +/-1 -> INCR / DECR,
// +/-2 to +/-255 -> ADDI / SUBI; a larger magnitude stays as two ops.
// It is pure stack arithmetic, safe even for a volatile value.
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

// A constant 0 under a no-op op (x | 0, x ^ 0, x << 0, x >> 0), a
// constant 1 under DIV (x / 1), or a double complement (~~x), leaves
// the value unchanged, so both ops go. The front end already folds
// x + 0 and x - 0, and rewrite_mul() does x * 1; these are the rest.
// Pure stack arithmetic, safe even for a volatile value.
static int rewrite_noop(Mop *a, Mop *b)
{
  int v;
  if (mop_const_value(a, &v)
      && ((v == 0 && IS_ZERO_IDENTITY(b->op))
          || (v == 1 && b->op == OP_DIV))) {
    nwin -= 2;
    return 1;
  }
  if ((a->op == OP_COMP || a->op == OP_NEG) && b->op == a->op) {
    nwin -= 2;
    return 1;
  }
  return 0;
}

// A byte load (LLB, LAB) already zero-extends, so a full-byte ANDI 255
// right after it is redundant and goes. Not a word load (LLW, LAW),
// whose high byte may be set. Pure stack, safe even for a volatile.
// Feeds on the const AND -> ANDI peep (c & 255 becomes LLB ANDI 255).
static int rewrite_load_andi(Mop *a, Mop *b)
{
  if (b->op == OP_ANDI && b->a == 0xFF
      && (a->op == OP_LLB || a->op == OP_LAB)) {
    nwin--;
    return 1;
  }
  return 0;
}

// DROP CN 0 (discard a value, then push 0) is one ANDI 0: it pops the
// value and pushes 0 in one op instead of two. The value is still
// computed, for its side effects; only the drop-and-replace is cheaper.
// Same two bytes, one fewer instruction. Pure stack, safe even for a
// volatile value.
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

// Two equal WORD (CW) constants in a row push the same value twice; the
// second is a DUP. A CW loads two operand bytes, so the stack copy (DUP,
// 44 cycles) is cheaper than a second CW (48) by 4 cycles and 2 bytes. The
// cheaper forms (MINUS1, CN, CB, CFFB) load from the opcode or one operand
// byte (36-43 cycles), where a DUP is a cycle regression or a wash, so they
// stay as two constants. Pure stack, safe even for a volatile value.
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

// Whether b loads again the value a left on the stack: the same load
// of a local or global twice (LLW n LLW n, also LLB, LAW and LAB, the
// even ops from LLB to LAW), or a keeping store then a load of what it
// stored (DLW n LLW n, also DLB, DAW and DAB). Neither may be
// volatile, which must be read each time; a volatile op is odd.
static int repeats_load(Mop *a, Mop *b)
{
  int op = b->op;
  if (op < OP_LLB || op > OP_LAW || (op & MOP_VOLATILE)
      || a->a != b->a || a->fix != b->fix || a->fn != b->fn)
    return 0;
  return a->op == op || keeping_store(plain_store(a->op), op) == a->op;
}

// The 16-bit pattern in v as a signed int, as parse.c's wrap16: the
// unsigned arithmetic works whether the host's int has 16 or 32 bits.
static int signed_word(int v)
{
  unsigned u = (unsigned)v & 0xFFFF;
  if (u & 0x8000)
    return -(int)(0xFFFF - u) - 1;
  return (int)u;
}

// c shifted right by y (0 to 15) as a signed 16-bit word, rounding
// toward minus infinity: the sign bit shifts in, as on every VM. The
// count is small, so a shift at a time is the whole op.
static int shr_word(int c, int y)
{
  unsigned u = (unsigned)c & 0xFFFF;
  while (y-- > 0)
    u = (u >> 1) | ((u & 1) != 0 ? 0x8000 : 0);
  return signed_word((int)u);
}

// If op folds two constants, c on the stack below a, set *v to the
// 16-bit value the VM's op would push and return 1; 0 if the peep
// must not fold it. The values are as signed words where the op is
// signed (the VM's words are two's complement): division and modulo
// are C's (toward zero), the comparisons give the VM's $FFFF / 0. The
// no cases are C's undefined ones: a zero divisor, where the VMs
// disagree, and a shift by sixteen or more.
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

// The patterns that end in a branch (b), with a the op before it.
// Returns 1 if one changed anything.
static int rewrite_branch(Mop *a, Mop *b)
{
  int v;

  // NEG BRFLS / BRTRU -> BRFLS / BRTRU: negating doesn't change
  // whether a value is 0, so a comparison's NEG (which makes C's 1)
  // isn't needed when the result only feeds a branch.
  if (a->op == OP_NEG && is_test_branch(b->op)) {
    *a = *b;
    nwin--;
    return 1;
  }
  // NOT ANDI n BRFLS / BRTRU -> BRTRU / BRFLS: branch on !x by testing x
  // with the opposite branch. The ANDI n (n non-zero) only normalizes the
  // truth value to 1 (PLASMA's NOT gives $FFFF), and a test branch cares
  // only about zero vs non-zero, so the mask is dropped. (Eliding it before
  // a non-branch truth use is NOT done: a following NOT belongs to another
  // !expr and would be clobbered.)
  if (nwin >= 3 && win[nwin - 3].op == OP_NOT && a->op == OP_ANDI
      && a->a != 0 && is_test_branch(b->op)) {
    win[nwin - 3] = *b;
    win[nwin - 3].op = invert_branch(b->op);
    nwin -= 2;
    return 1;
  }
  // NOT BRFLS / BRTRU -> BRTRU / BRFLS: branch on !x by testing x.
  if (a->op == OP_NOT && is_test_branch(b->op)) {
    *a = *b;
    a->op = invert_branch(b->op);
    nwin--;
    return 1;
  }
  // ISEQ / ISNE then BRFLS / BRTRU -> BREQ / BRNE: compare and branch
  // in one op.
  if ((a->op == OP_ISEQ || a->op == OP_ISNE) && is_test_branch(b->op)) {
    int on_true = b->op == OP_BRTRU;
    int on_equal = (a->op == OP_ISEQ) == on_true;
    *a = *b;
    a->op = on_equal ? OP_BREQ : OP_BRNE;
    nwin--;
    return 1;
  }
  // CN 0 BREQ / BRNE -> BRFLS / BRTRU: comparing with 0 is just a
  // test of the value.
  if ((b->op == OP_BREQ || b->op == OP_BRNE)
      && mop_const_value(a, &v) && v == 0) {
    *a = *b;
    a->op = b->op == OP_BREQ ? OP_BRFLS : OP_BRTRU;
    nwin--;
    return 1;
  }
  return 0;
}

// const MUL -> cheaper ops for some constants, as PLASMA's own
// optimizer does (src/toolsrc/codeopt.pla), with a the constant and b
// the MUL. MUL is a 16-step shift-and-add loop on the 6502 VMs, so it
// is the slowest choice. Returns 1 if it changed anything.
static int rewrite_mul(Mop *a, Mop *b)
{
  int v;

  // x % 1 -> DROP CN 0, as x * 0 below: C's x % 1 is 0 for every int
  // (unlike x / -1, it can't overflow).
  if (b->op == OP_MOD && mop_const_value(a, &v) && v == 1) {
    a->op = OP_DROP;
    mop_const(b, 0);
    return 1;
  }
  if (b->op == OP_MUL && mop_const_value(a, &v)) {
    // x * 2 -> DUP ADD, not CN 1 SHL. Both are 2 bytes, but on the
    // 6502 VM DUP ADD takes about 52 cycles and CN 1 SHL about 59:
    // SHL is a general shifter that saves Y, checks for counts of 8
    // or more, and loops. (codeopt.pla even turns CN 1 SHL into
    // DUP ADD.) It also lets p + i fuse into IDXW, below.
    if (v == 2) {
      a->op = OP_DUP;
      a->a = 0;
      b->op = OP_ADD;
      return 1;
    }
    // x * 3 -> DUP IDXW: x + 2x, in 2 bytes.
    if (v == 3) {
      a->op = OP_DUP;
      a->a = 0;
      b->op = OP_IDXW;
      return 1;
    }
    // x * 0 -> DROP CN 0: x is still computed, for its side
    // effects (such as a call), then replaced by 0.
    if (v == 0) {
      a->op = OP_DROP;
      mop_const(b, 0);
      return 1;
    }
    // x * 1 -> x: nothing to do.
    if (v == 1) {
      nwin -= 2;
      return 1;
    }
    // x * 2^n, n from 2 to 15 -> CN n SHL: the same size as the
    // MUL, and much faster. (Not for x / 2^n: SHR rounds toward
    // minus infinity, but C's / truncates toward zero.)
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

static void remove_op(int i);
static int drop_expr(int i);

// The window ends in a constant and a binary op whose result is the
// constant v whatever x, the value under them, is: make them DROP v,
// so x is still computed for its side effects, and then, if x has no
// effect (drop_expr()), remove it and the DROP too.
static void drop_for_const(int v)
{
  Mop *a = &win[nwin - 2];
  a->op = OP_DROP;
  a->a = 0;
  mop_const(&win[nwin - 1], v);
  drop_expr(nwin - 2);
}

// The constant -1 ($FFFF) under a binary op, x op -1: x * -1 and
// x / -1 are -x (NEG; -32768 / -1 overflows, which is undefined in C);
// x % -1 is 0 (the same); x & -1 is x; x | -1 is -1; and x ^ -1 is ~x
// (COMP). Pure stack arithmetic, safe even for a volatile x, which is
// still read.
static int rewrite_minus1(Mop *a, Mop *b)
{
  int v;
  if (a->fix != FIX_NONE || !mop_const_value(a, &v)
      || (v & 0xFFFF) != 0xFFFF)
    return 0;
  if (b->op == OP_MUL || b->op == OP_DIV || b->op == OP_XOR) {
    a->op = b->op == OP_XOR ? OP_COMP : OP_NEG;
    a->a = 0;
    nwin--;
    return 1;
  }
  if (b->op == OP_AND) {
    nwin -= 2;
    return 1;
  }
  if (b->op == OP_MOD || b->op == OP_IOR) {
    drop_for_const(b->op == OP_MOD ? 0 : -1);
    return 1;
  }
  return 0;
}

// A value with itself: DUP SUB and DUP XOR are 0 (x - x, x ^ x), and
// DUP AND and DUP IOR are x (x & x, x | x). Only a DUP: two loads of a
// volatile are two reads, which must stay (repeats_load() makes no DUP
// of one), but a DUP's value was read once, so this reads it once.
static int rewrite_self(Mop *a, Mop *b)
{
  if (a->op != OP_DUP)
    return 0;
  if (b->op == OP_SUB || b->op == OP_XOR) {
    drop_for_const(0);
    return 1;
  }
  if (b->op == OP_AND || b->op == OP_IOR) {
    nwin -= 2;
    return 1;
  }
  return 0;
}

// Remove the DROP at window index i and, if the expression before it
// has no effect (expr_len(): plain loads and arithmetic), that too.
// Returns how many ops before i went.
static int drop_expr(int i)
{
  int n = expr_len(i), k;
  if (n == 0)
    return 0;
  for (k = 0; k <= n; k++)
    remove_op(i - n);
  return n;
}


// The window's last op is a LEAVE or RET: remove the stores to locals
// before it that nothing reads back (the patterns at the top). It scans
// back over ops that only work on the stack (stack_effect(), but no
// load through an address, which might read a local through a pointer),
// DROPs, loads of locals, and the stores, and stops at anything else.
static void dead_stores(void)
{
  int i, j, pops, pushes, nread = 0, read[WINDOW], op;
  Mop *m;
  for (i = nwin - 2; i >= 0; i--) {
    m = &win[i];
    op = m->op;
    if (STORES_LOCAL(op)) {
      for (j = 0; j < nread; j++)
        if (m->a - read[j] <= 1 && read[j] - m->a <= 1)
          break;
      if (j < nread)
        continue;               // loaded back: not dead
      if (op == OP_DLW) {
        remove_op(i);
      } else if (op == OP_DLB) {
        m->op = OP_ANDI;
        m->a = 0xFF;
      } else {
        m->op = OP_DROP;
        m->a = 0;
        i -= drop_expr(i);      // and the value, if it has no effect
      }
    } else if (READS_LOCAL(op)) {
      read[nread++] = m->a;
    } else if (op != OP_DROP && op != OP_DROP2
               && (!stack_effect(m, &pops, &pushes) || op == OP_LW
                   || op == OP_LB)) {
      break;
    }
  }
}

// Try one pattern at the end of the window. Returns 1 if it changed
// anything, so the caller tries again.
//
// It runs after every op, and most passes match nothing (71% of them,
// compiling ccish's own source), so each rule's call has a guard: a
// cheap test, of b's op, a's op, or whether a is a constant, that its
// rule can't match without. A pass then skips the calls (about 150
// cycles each, before any test in them) of the rules that can't apply:
// ccgen ran 12.7% fewer VM ops, for a few bytes a rule. Each guard
// sits at its rule's call and states that rule's own condition, so the
// two are easy to keep in step, and a guard that wrongly stops its
// rule shows in the rule's gen test, as code no longer optimized.
// The guards aren't grouped further, as one test of b's op choosing a
// group of rules: that would save little more, but such a test sits
// apart from the rules it must agree with, so a rule changed to match
// a new op could silently stop firing, and it nests the rules, whose
// order changes what they make, one level deeper.
static int rewrite(void)
{
  Mop *a, *b;
  int op, aop, ac;

  if (nwin < 2)
    return 0;
  a = &win[nwin - 2];
  b = &win[nwin - 1];
  op = b->op;
  aop = a->op;
  ac = MOP_IS_CONST(aop);
  // const OP const -> const (fold_pair()): both sides constant is a
  // backstop for the parser's own folding. It comes first, so that
  // the one-constant rewrites below don't spend one constant into an
  // ADDI or the like, leaving the other stranded.
  if (ac && nwin >= 3) {
    int v1, v2, v;
    if (mop_const_value(&win[nwin - 3], &v1)
        && mop_const_value(a, &v2) && fold_pair(op, v1, v2, &v)) {
      Mop r;
      mop_const(&r, v);
      win[nwin - 3] = r;
      nwin -= 2;
      return 1;
    }
  }
  // The rules, in order, each behind its guard (above IS_ADD_VALUE, and
  // this function's comment).
  if (rewrite_load_store(a, b)
      || (ac && rewrite_const(a, b))
      || (IS_VALUE_BRANCH(op) && rewrite_branch(a, b))
      || (ac && (rewrite_mul(a, b) || rewrite_minus1(a, b)))
      || (aop == OP_DUP && rewrite_self(a, b))
      || (op == OP_ANDI && rewrite_andi(a, b))
      || (IS_ADD_VALUE(op) && rewrite_addsub(a, b))
      || ((ac || IS_NEG_COMP(aop)) && rewrite_noop(a, b))
      || (op == OP_ANDI && rewrite_load_andi(a, b))
      || (aop == OP_DROP && rewrite_drop_zero(a, b))
      || (aop == OP_CW && rewrite_dup_const(a, b))
      || ((op == OP_ADD || op == OP_IDXW) && rewrite_index(a, b)))
    return 1;
  // NOT ANDI n NOT -> NOT NOT: !!x. The ANDI n (n non-zero) only makes
  // the first NOT's true ($FFFF) into 1, and NOT tests only zero
  // against non-zero, so the second NOT gives the same without it.
  if (op == OP_NOT && aop == OP_ANDI && a->a != 0 && nwin >= 3
      && win[nwin - 3].op == OP_NOT) {
    *a = *b;
    nwin--;
    return 1;
  }
  // expr ANDI 0 -> CN 0: x & 0 is 0, and so are x * 0 and x % 1, which
  // become DROP CN 0, then ANDI 0. When x has no effect (expr_len()),
  // it goes; a call or a volatile read keeps its ANDI 0.
  if (op == OP_ANDI && b->a == 0) {
    int n = expr_len(nwin - 1);
    if (n > 0) {
      nwin -= n;
      mop_const(&win[nwin - 1], 0);
      return 1;
    }
  }
  // 0 op x, for an expression x with no effect (expr_len(): plain
  // loads and arithmetic): CN 0 expr SUB -> expr NEG, as 0 - x is -x;
  // and CN 0 expr SHL / SHR / DIV / MOD -> CN 0, as 0 << x, 0 >> x,
  // 0 / x and 0 % x are 0 (dividing by 0 is undefined in C).
  if (IS_ZERO_LEFT(op)) {
    int n = expr_len(nwin - 1), i = nwin - 2 - n, v;
    if (n > 0 && i >= 0 && win[i].fix == FIX_NONE
        && mop_const_value(&win[i], &v) && v == 0) {
      if (op == OP_SUB) {
        remove_op(i);
        win[nwin - 1].op = OP_NEG;
      } else {
        nwin = i + 1;
      }
      return 1;
    }
  }
  // const m SHL const n SHL -> const (m + n) SHL: (x << m) << n is
  // x << (m + n), and 0 when that's 16 or more; the same for SHR, whose
  // count stops at 15, since an arithmetic shift by 15 already fills
  // the value with its sign. Pure stack arithmetic.
  if (nwin >= 4 && (op == OP_SHL || op == OP_SHR)
      && win[nwin - 3].op == op && win[nwin - 4].fix == FIX_NONE
      && a->fix == FIX_NONE) {
    int m, n;
    if (mop_const_value(&win[nwin - 4], &m) && mop_const_value(a, &n)
        && m >= 0 && m <= 15 && n >= 0 && n <= 15) {
      nwin -= 2;
      if (m + n <= 15 || op == OP_SHR)
        mop_const(&win[nwin - 2], m + n <= 15 ? m + n : 15);
      else
        drop_for_const(0);
      return 1;
    }
  }
  // DUP ADD ADD -> IDXW: adding twice x is PLASMA's word index op
  // (p + i for a pointer to 2-byte elements).
  if (op == OP_ADD && aop == OP_ADD && nwin >= 3
      && win[nwin - 3].op == OP_DUP) {
    win[nwin - 3].op = OP_IDXW;
    nwin -= 2;
    return 1;
  }
  // LLW n LLW n op -> LLW n DUP op (repeats_load()): the second load is
  // a DUP, 10 to 28 cycles faster and a byte or two smaller. Only once
  // an op follows that no rule above joined to it, since a load that
  // fuses with what follows (LLW n ADD -> ADDLW n) beats DUP ADD.
  if (aop >= OP_LLB && aop <= OP_LAW && nwin >= 3
      && repeats_load(&win[nwin - 3], a)) {
    a->op = OP_DUP;
    a->a = 0;
    a->fix = FIX_NONE;
    return 1;
  }
  // DUP update store DROP -> update store: x++ (or a similar update)
  // whose old value is unused needs no copy of it; nor does a store.
  if (op == OP_DROP && drop_dup())
    return 1;
  // expr DROP -> nothing, for an expression that reads no memory but
  // the frame.
  if (op == OP_DROP && drop_expr(nwin - 1))
    return 1;
  return 0;
}

// The index of the last op in the window before index i that isn't a
// join (which emits no code), or -1.
static int code_before(int i)
{
  while (--i >= 0 && win[i].op == OP_JOIN)
    ;
  return i;
}

// Remove the op at window index i.
static void remove_op(int i)
{
  for (; i < nwin - 1; i++)
    win[i] = win[i + 1];
  nwin--;
}

// The patterns at label n, just before it is placed (which flushes the
// window). Joins in between are skipped.
static void before_label(int n)
{
  int b = code_before(nwin), a;
  if (b < 0 || win[b].op != OP_BRNCH)
    return;
  // BRNCH n, then label n -> the label alone: the jump goes to the
  // very next op.
  if (win[b].a == n) {
    remove_op(b);
    return;
  }
  // Bcond n BRNCH t, then label n -> Binv t, then label n: branch to t
  // on the opposite test, else fall through to n (BRFLS / BRTRU,
  // BREQ / BRNE). a && b || c and !(a && b) need this BRNCH.
  a = code_before(b);
  if (a >= 0 && invert_branch(win[a].op) >= 0 && win[a].a == n) {
    win[a].op = invert_branch(win[a].op);
    win[a].a = win[b].a;
    remove_op(b);
  }
}

// Stage 3's entry: add an op to the window and apply the patterns.
// Barriers flush the window, and ops in dead code are dropped. A join
// emits no code, so it isn't a barrier: it waits in the window, in
// order (stage 4 needs the branches it moves first), and is kept even
// in dead code (the branches it moves are live).
void peep_put(Mop *m)
{
  int op = m->op;
  if (op == OP_JOIN) {
    if (nwin == WINDOW)
      pass_oldest();
    win[nwin++] = *m;
    return;
  }
  if (MOP_IS_PSEUDO(op)) {
    // A barrier: never optimize across it.
    if (op == OP_LABEL)
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
  if (IS_EXIT(op))
    dead = 1;
  if (op == OP_LEAVE || op == OP_RET)
    dead_stores();
  while (rewrite())
    ;
}
