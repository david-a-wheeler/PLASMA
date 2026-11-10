// Stage 2: lowering, the first stage of the back end (ccgen). Turns IR
// ops, as irin.c reads them from the IR file, into PLASMA machine ops
// and chooses their encodings, mostly 1:1. Each IR op is described in
// ../IR.md. -i prints each op (dump_ir()) as irin.c reads it.
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "obj.h"
#include "obj.fmt.h"
#include "ir.h"
#include "mop.h"
#include "irin.h"
#include "lower.h"
#include "peep.h"
#include "helper.h"

// Each IR op's name, for -i; 0 for a number that is no op, which
// irin.c refuses before it prints one.
static const char *const ir_names[] = {
  // $00: local localb setlocal setlocalb
  "LOCAL", "LOCALB", "SETLOCAL", "SETLOCALB",
  // $04: global globalb setglobal setglobalb
  "GLOBAL", "GLOBALB", "SETGLOBAL", "SETGLOBALB",
  // $08: load loadb store storeb
  "LOAD", "LOADB", "STORE", "STOREB",
  // $0C: localaddr dataaddr heapaddr straddr
  "LOCALADDR", "DATAADDR", "HEAPADDR", "STRADDR",
  // $10: func call funcaddr -
  "FUNC", "CALL", "FUNCADDR", 0,
  // $14: endfunc const drop dup
  "ENDFUNC", "CONST", "DROP", "DUP",
  // $18: return icall swap reglocal
  "RETURN", "ICALL", "SWAP", "REGLOCAL",
  // $1C: neg comp not -
  "NEG", "COMP", "NOT", 0,
  // $20: localv localbv setlocalv setlocalbv
  "LOCALV", "LOCALBV", "SETLOCALV", "SETLOCALBV",
  // $24: globalv globalbv setglobalv setglobalbv
  "GLOBALV", "GLOBALBV", "SETGLOBALV", "SETGLOBALBV",
  // $28: loadv loadbv storev storebv
  "LOADV", "LOADBV", "STOREV", "STOREBV",
  // $2C: udiv umod ushr -
  "UDIV", "UMOD", "USHR", 0,
  // $30: add sub mul and
  "ADD", "SUB", "MUL", "AND",
  // $34: or xor - -
  "OR", "XOR", 0, 0,
  // $38: div mod shl shr
  "DIV", "MOD", "SHL", "SHR",
  // $3C: - - - -
  0, 0, 0, 0,
  // $40: lt ge gt le
  "LT", "GE", "GT", "LE",
  // $44: eq ne - -
  "EQ", "NE", 0, 0,
  // $48: ult uge ugt ule
  "ULT", "UGE", "UGT", "ULE",
  // $4C: - - - -
  0, 0, 0, 0,
  // $50: jump - brfalse brtrue
  "JUMP", 0, "BRFALSE", "BRTRUE",
  // $54: sel caseto - -
  "SEL", "CASETO", 0, 0,
  // $58: label join cases caseval
  "LABEL", "JOIN", "CASES", "CASEVAL",
  // $5C: loop loopstep loopbody loopend
  "LOOP", "LOOPSTEP", "LOOPBODY", "LOOPEND",
};
// One name per op number; sized by its entries, so a missing or extra
// row is caught here.
_Static_assert(sizeof(ir_names) / sizeof(ir_names[0]) == IR_NOPS,
               "ir_names[] doesn't have one name per IR op number");

// How each IR op lowers (lw_kind[]), and the PLASMA opcode it lowers
// to (lw_op[]), as parallel arrays indexed by IR op:
//   L_SIMPLE   that opcode, with no operand
//   L_COMPARE  that comparison, then NEG: PLASMA's comparisons give
//              $FFFF for true, on every VM, and NEG makes it C's 1 (when
//              the result only feeds a branch, the peephole stage removes
//              the NEG)
//   L_OPERAND  that opcode, with the IR op's operand a (a frame offset,
//              a branch's label, or a pseudo-op's operand)
//   L_GLOBAL   that opcode, the absolute-address form, with a's address
//              relocated as data (data_fix())
//   L_SPECIAL  lowered by lower_special()
//   L_NONE     no op: an UNUSED number in ir.h (IR_IS_OP, lower.h)
// The stores keep their value (DLW, DLB, DAW, DAB): an assignment's
// value can be used, and when it isn't, the following DROP lets the
// peephole stage make the plain store (SLW, ...). DLB keeps the stored
// byte, which is the value C gives an assignment to a char.
enum { L_NONE, L_SIMPLE, L_COMPARE, L_OPERAND, L_GLOBAL, L_SPECIAL };
_Static_assert(L_NONE == 0, "IR_IS_OP (lower.h) tests lw_kind[] for 0");

const char lw_kind[] = {
  // $00: local localb setlocal setlocalb
  L_OPERAND, L_OPERAND, L_OPERAND, L_OPERAND,
  // $04: global globalb setglobal setglobalb
  L_GLOBAL, L_GLOBAL, L_GLOBAL, L_GLOBAL,
  // $08: load loadb store storeb
  L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE,
  // $0C: localaddr dataaddr heapaddr straddr
  L_OPERAND, L_GLOBAL, L_SPECIAL, L_SPECIAL,
  // $10: func call funcaddr -
  L_SPECIAL, L_SPECIAL, L_SPECIAL, L_NONE,
  // $14: endfunc const drop dup
  L_SPECIAL, L_SPECIAL, L_SIMPLE, L_SIMPLE,
  // $18: return icall swap reglocal
  L_SPECIAL, L_SIMPLE, L_SPECIAL, L_SPECIAL,
  // $1C: neg comp not -
  L_SIMPLE, L_SIMPLE, L_SPECIAL, L_NONE,
  // $20: localv localbv setlocalv setlocalbv
  L_OPERAND, L_OPERAND, L_OPERAND, L_OPERAND,
  // $24: globalv globalbv setglobalv setglobalbv
  L_GLOBAL, L_GLOBAL, L_GLOBAL, L_GLOBAL,
  // $28: loadv loadbv storev storebv
  L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE,
  // $2C: udiv umod ushr -
  L_SPECIAL, L_SPECIAL, L_SPECIAL, L_NONE,
  // $30: add sub mul and
  L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE,
  // $34: or xor - -
  L_SIMPLE, L_SIMPLE, L_NONE, L_NONE,
  // $38: div mod shl shr
  L_SIMPLE, L_SIMPLE, L_SIMPLE, L_SIMPLE,
  // $3C: - - - -
  L_NONE, L_NONE, L_NONE, L_NONE,
  // $40: lt ge gt le
  L_COMPARE, L_COMPARE, L_COMPARE, L_COMPARE,
  // $44: eq ne - -
  L_COMPARE, L_COMPARE, L_NONE, L_NONE,
  // $48: ult uge ugt ule
  L_COMPARE, L_COMPARE, L_COMPARE, L_COMPARE,
  // $4C: - - - -
  L_NONE, L_NONE, L_NONE, L_NONE,
  // $50: jump - brfalse brtrue
  L_OPERAND, L_NONE, L_OPERAND, L_OPERAND,
  // $54: sel caseto - -
  L_OPERAND, L_OPERAND, L_NONE, L_NONE,
  // $58: label join cases caseval
  L_OPERAND, L_SPECIAL, L_OPERAND, L_OPERAND,
  // $5C: loop loopstep loopbody loopend
  L_SPECIAL, L_SPECIAL, L_SPECIAL, L_SPECIAL,
};
// As ir_names[]: one entry per op number.
_Static_assert(sizeof(lw_kind) == IR_NOPS,
               "lw_kind[] doesn't have one entry per IR op number");

static const unsigned char lw_op[] = {
  // $00: local localb setlocal setlocalb
  OP_LLW, OP_LLB, OP_DLW, OP_DLB,
  // $04: global globalb setglobal setglobalb
  OP_LAW, OP_LAB, OP_DAW, OP_DAB,
  // $08: load loadb store storeb
  OP_LW, OP_LB, OP_SW, OP_SB,
  // $0C: localaddr dataaddr heapaddr straddr
  OP_LLA, OP_LA, OP_LA, 0,
  // $10: func call funcaddr -
  0, 0, 0, 0,
  // $14: endfunc const drop dup
  0, 0, OP_DROP, OP_DUP,
  // $18: return icall swap reglocal
  0, OP_ICAL, 0, 0,
  // $1C: neg comp not -
  OP_NEG, OP_COMP, 0, 0,
  // $20: localv localbv setlocalv setlocalbv
  OP_LLW | MOP_VOLATILE, OP_LLB | MOP_VOLATILE,
  OP_DLW | MOP_VOLATILE, OP_DLB | MOP_VOLATILE,
  // $24: globalv globalbv setglobalv setglobalbv
  OP_LAW | MOP_VOLATILE, OP_LAB | MOP_VOLATILE,
  OP_DAW | MOP_VOLATILE, OP_DAB | MOP_VOLATILE,
  // $28: loadv loadbv storev storebv
  OP_LW | MOP_VOLATILE, OP_LB | MOP_VOLATILE,
  OP_SW | MOP_VOLATILE, OP_SB | MOP_VOLATILE,
  // $2C: udiv umod ushr -
  0, 0, 0, 0,
  // $30: add sub mul and
  OP_ADD, OP_SUB, OP_MUL, OP_AND,
  // $34: or xor - -
  OP_IOR, OP_XOR, 0, 0,
  // $38: div mod shl shr
  OP_DIV, OP_MOD, OP_SHL, OP_SHR,
  // $3C: - - - -
  0, 0, 0, 0,
  // $40: lt ge gt le
  OP_ISLT, OP_ISGE, OP_ISGT, OP_ISLE,
  // $44: eq ne - -
  OP_ISEQ, OP_ISNE, 0, 0,
  // $48: ult uge ugt ule
  OP_ISLT, OP_ISGE, OP_ISGT, OP_ISLE,
  // $4C: - - - -
  0, 0, 0, 0,
  // $50: jump - brfalse brtrue
  OP_BRNCH, 0, OP_BRFLS, OP_BRTRU,
  // $54: sel caseto - -
  OP_SEL, OP_CASETO, 0, 0,
  // $58: label join cases caseval
  OP_LABEL, 0, OP_CASES, OP_CASEVAL,
  // $5C: loop loopstep loopbody loopend
  0, 0, 0, 0,
};
// As ir_names[]: one entry per op number.
_Static_assert(sizeof(lw_op) == IR_NOPS,
               "lw_op[] doesn't have one entry per IR op number");

// Write s, then n in decimal, to standard output, for the -i and -d
// dumps. No printf: the small build has none.
void out_num(const char *s, int n)
{
  fputs(s, stdout);
  fputs(num_text(n), stdout);
}

// Print one IR op, for -i: "IR", its name padded to 8 characters, and
// its operands. The ops that print alike are mostly blocks of IR
// numbers, so ranges find them; a switch takes 4 bytes a case.
void dump_ir(int op, int a, int f)
{
  int n = (int)strlen(ir_names[op]);
  fputs("IR ", stdout);
  fputs(ir_names[op], stdout);
  while (n++ < 8)
    fputs(" ", stdout);
  if (IR_IS_LOOP_MARKER(op))
    op = IR_JOIN;               // a loop marker's two labels, the same
  if (op <= IR_SETLOCALB || op == IR_LOCALADDR || op == IR_REGLOCAL) {
    out_num(" [", a);
    fputs("]", stdout);
  } else if (op <= IR_SETGLOBALB || op == IR_DATAADDR) {
    if (f >= 0) {
      out_num(" sym", f);
      out_num("+", a);
    } else {
      out_num(" data+", a);
    }
  } else if (IR_REFS_LABEL(op) || op == IR_LABEL) {
    out_num(" L", a);
  } else if (op == IR_CONST || op == IR_CASES || op == IR_CASEVAL
             || IR_IS_U_COMPARE(op) || (op >= IR_UDIV && op <= IR_USHR)) {
    out_num(" ", a);
  } else {
    switch (op) {
    case IR_FUNC: case IR_FUNCADDR:
      fputs(" ", stdout);
      fputs(func_name(f), stdout);
      break;
    case IR_CALL:
      fputs(" ", stdout);
      fputs(func_name(f), stdout);
      out_num(" nargs=", a);
      break;
    case IR_ENDFUNC:  out_num(" frame=", a); break;
    case IR_STRADDR:  out_num(" string+", a); break;
    case IR_HEAPADDR: out_num(" sym", f); break;
    case IR_ICALL:    out_num(" nargs=", a); out_num(" words=", f); break;
    case IR_JOIN:     out_num(" L", a); out_num(" L", f); break;
    case IR_SWAP:     out_num(" ", a); out_num(" ", f); break;
    }
  }
  fputs("\n", stdout);
}

// The fixup for a global op's address: a data offset (f is -1), or
// extern global f plus the addend in m->a, which m->fn then names.
static int data_fix(Mop *m, int f)
{
  m->fn = f;
  return f < 0 ? FIX_DATA : FIX_DSYM;
}

// Make m push the constant c, using the shortest encoding (the same
// choice as the PLASMA compiler's emit_const). The range tests are on
// it as unsigned, v: on a 16-bit host, an int from $8000 up is
// negative.
void mop_const(Mop *m, int c)
{
  unsigned v = (unsigned)c & 0xFFFF;
  memset(m, 0, sizeof(*m));
  if (v == 0xFFFF) {
    m->op = OP_MINUS1;
  } else if (v < 16) {
    m->op = MOP_CN(v);
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

// If m pushes a constant, store it (as 16 bits) in *v and return 1.
// On a 16-bit host, a value from $8000 up is negative, so test *v with
// masks, not ranges.
int mop_const_value(Mop *m, int *v)
{
  if (MOP_IS_CN(m->op)) {
    *v = MOP_CN_VALUE(m->op);
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

// Pass an operand-free machine op to the peephole stage.
static void put_simple(int op)
{
  Mop m;
  memset(&m, 0, sizeof(m));
  m.op = op;
  peep_put(&m);
}

// Pass a frame op (LLW, SLW, ...) on one of the back end's own
// temporaries, k, to the peephole stage.
static void put_temp(int op, int k)
{
  Mop m;
  memset(&m, 0, sizeof(m));
  m.op = op;
  m.a = FRAME_TEMP(k);
  peep_put(&m);
}

// Make m a call of helper k, which this object must then hold.
static void helper_call(Mop *m, int k)
{
  m->op = OP_CALL;
  m->fix = FIX_HELPER;
  m->fn = k;
  helper_used |= 1 << k;
}

// Lower the IR ops of kind L_SPECIAL into m (and any ops before it).
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
  case IR_FUNCADDR:
    m->op = op == IR_CALL ? OP_CALL : OP_LA;
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
    // C's !x is 1 or 0; PLASMA's NOT gives $FFFF (true) or 0 on
    // every VM, so mask it: NOT then AND 1 gives 1 or 0. When the
    // result only feeds a truth test (a branch, or another NOT), the
    // peephole stage drops the ANDI, since $FFFF is already "true".
    put_simple(OP_NOT);
    m->op = OP_ANDI;
    m->a = 1;
    break;
  case IR_USHR:
    // x >> n unsigned. By a constant n (f; irin.c made / by 2^n one
    // too): PLASMA's SHR is arithmetic, so mask off the copies of the
    // sign bit it shifts in. By a variable: the helper.
    if (f < 0) {
      helper_call(m, HELP_USHR);
    } else if (f == 0) {
      m->op = OP_SHR;           // by 0: nothing to mask
    } else {
      put_simple(OP_SHR);
      mop_const(m, 0xFFFF >> f);
      peep_put(m);
      memset(m, 0, sizeof(*m));
      m->op = OP_AND;
    }
    break;
  case IR_UDIV: case IR_UMOD:
    // By other than a power of 2 (irin.c did those): the helper.
    helper_call(m, op == IR_UDIV ? HELP_UDIV : HELP_UMOD);
    break;
  case IR_SWAP:
    // What irin.c couldn't do by moving code: exchange the top value
    // (a = 1), or the top value and its copy (a = 2), with the value
    // below, through two temporaries.
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
    fatal0(174 /* "internal error: unknown IR op" */);
  }
}

// Stage 2's entry: lower one IR op to machine ops and pass them to the
// peephole stage, as lw_kind[] and lw_op[] say.
void lower_op(int op, int a, int f)
{
  Mop m;
  int kind;

  memset(&m, 0, sizeof(m));
  if (op == IR_MOP) {
    m.op = a;                   // a counted loop's branch, to label f
    m.a = f;
    peep_put(&m);
    return;
  }
  kind = lw_kind[op];
  m.op = lw_op[op];
  if (kind == L_COMPARE) {
    if (IR_IS_U_COMPARE(op)) {
      // CMDSYS's native ISULT and the rest (irin.c): $FFFF or 0, as
      // ISLT gives.
      helper_call(&m, HELP_U_COMPARE(op));
      peep_put(&m);
      memset(&m, 0, sizeof(m));
    } else {
      put_simple(m.op);
    }
    m.op = OP_NEG;
  } else if (kind == L_SPECIAL) {
    lower_special(&m, op, a, f);
  } else if (kind != L_SIMPLE) {
    m.a = a;
    if (kind == L_GLOBAL)
      m.fix = data_fix(&m, f);
  }
  peep_put(&m);
  if (op == IR_ENDFUNC)
    peep_flush();
}
