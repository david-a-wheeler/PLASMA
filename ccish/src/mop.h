// ccgen's machine ops (approach.md, "Stage 2 -> 3 -> 4"): PLASMA's
// opcodes and ccgen's pseudo-ops, as stages 2 to 4 (lower.c, peep.c,
// emit.c) pass them along.
#ifndef MOP_H
#define MOP_H

// PLASMA opcodes used so far (see ../../doc/Bytecodes.md). The VM
// numbers them in families the code relies on without saying so
// (approach.md, decision 26):
// - the constant pushes: CN 0-15 ($00-$1E), MINUS1, CB, CW, CFFB
//   (mop_const_value in lower.c recognizes exactly these);
// - the zero tests, popping no value: BREQ, BRNE ($22, $24);
// - the comparisons of two values: ISEQ through ISLE ($40-$4A);
// - the test branches, popping one: BRFLS, BRTRU ($4C, $4E);
// - the branches and calls: BRNCH, SEL, CALL, ENTER, LEAVE, RET
//   ($50-$5C);
// - the eight loads, bytes and words adjacent: LB LW, LLB LLW, LAB
//   LAW, DLB DLW ($60-$6E);
// - the eight stores, the same way: SB SW, SLB SLW, SAB SAW, DAB DAW
//   ($70-$7E);
// - the stack arithmetic, no memory: NOT, ADD through IDXW
//   ($80-$9E);
// - the counted-loop branches: BRGT through SUBBRGE ($A0-$AA);
// - the frame-address adds: ADDLB through IDXAW ($B0-$BE), in the
//   loads' order (LL, L, A, A forms), local the lower two of each
//   four (fused_op in peep.c and emit.c's op & 4 rely on it).
enum {
  OP_CN = 0x00, OP_MINUS1 = 0x20, OP_BREQ = 0x22, OP_BRNE = 0x24,
  OP_LA = 0x26, OP_LLA = 0x28, OP_CB = 0x2A,
  OP_CW = 0x2C, OP_CS = 0x2E, OP_DROP = 0x30, OP_DROP2 = 0x32, OP_DUP = 0x34,
  OP_ADDI = 0x38, OP_SUBI = 0x3A, OP_ANDI = 0x3C, OP_ORI = 0x3E,
  OP_ISEQ = 0x40, OP_ISNE = 0x42, OP_ISGT = 0x44, OP_ISLT = 0x46,
  OP_ISGE = 0x48, OP_ISLE = 0x4A, OP_BRFLS = 0x4C, OP_BRTRU = 0x4E,
  OP_BRNCH = 0x50, OP_SEL = 0x52,
  OP_CALL = 0x54, OP_ICAL = 0x56, OP_ENTER = 0x58, OP_LEAVE = 0x5A, OP_RET = 0x5C,
  OP_CFFB = 0x5E, OP_LB = 0x60, OP_LW = 0x62, OP_LLB = 0x64,
  OP_LLW = 0x66, OP_LAB = 0x68, OP_LAW = 0x6A, OP_DLB = 0x6C,
  OP_DLW = 0x6E, OP_SB = 0x70, OP_SW = 0x72, OP_SLB = 0x74, OP_SLW = 0x76,
  OP_SAB = 0x78, OP_SAW = 0x7A, OP_DAB = 0x7C, OP_DAW = 0x7E,
  OP_NOT = 0x80, OP_ADD = 0x82, OP_SUB = 0x84, OP_MUL = 0x86, OP_DIV = 0x88,
  OP_MOD = 0x8A, OP_INCR = 0x8C, OP_DECR = 0x8E, OP_NEG = 0x90,
  OP_COMP = 0x92, OP_AND = 0x94, OP_IOR = 0x96, OP_XOR = 0x98,
  OP_SHL = 0x9A, OP_SHR = 0x9C, OP_IDXW = 0x9E,
  OP_BRGT = 0xA0, OP_BRLT = 0xA2, OP_INCBRLE = 0xA4, OP_ADDBRLE = 0xA6,
  OP_DECBRGE = 0xA8, OP_SUBBRGE = 0xAA,   // counted loops (irin.c)
  OP_ADDLB = 0xB0, OP_ADDLW = 0xB2, OP_ADDAB = 0xB4,
  OP_ADDAW = 0xB6, OP_IDXLB = 0xB8, OP_IDXLW = 0xBA, OP_IDXAB = 0xBC,
  OP_IDXAW = 0xBE,
  // Pseudo-ops: markers that travel through stages 2-4 in order with
  // the code, but emit no bytes themselves. They are odd numbers below
  // $20 (MOP_IS_PSEUDO), which no PLASMA opcode is, so that every
  // machine op is a byte.
  OP_FUNC_BEGIN = 0x01,   // fn: a function starts here
  OP_FUNC_END = 0x03,     // a = frame size: the function ends here
  OP_LABEL = 0x05,        // a = label number: a branch target is here
  // A SEL's case table (see ../../doc/Bytecodes.md), as data in the
  // code:
  OP_CASES = 0x07,        // a = the count byte
  OP_CASEVAL = 0x09,      // a = a case's value word
  OP_CASETO = 0x0B,       // a = the label its offset word leads to
  OP_JOIN = 0x0D          // the branches so far to label a go to
                          // label fn instead
};

// The bit tricks on machine op values, each here so that a change to
// the numbers above shows what it touches. Every PLASMA opcode is
// even, and CN n is the opcode 2n ($00-$1E); the pseudo-ops are the
// odd values below $20. (approach.md issue 25 plans bit 0 of a real
// load or store, $20 up, as its volatile mark.)
#define MOP_IS_CN(op)      ((op) < 0x20 && !((op) & 1))
#define MOP_IS_PSEUDO(op)  ((op) < 0x20 && ((op) & 1))
// A volatile load or store (approach.md issue 25) is its opcode |
// MOP_VOLATILE: an odd op from $20 up, which no pattern written for
// the plain one matches; emit.c writes the plain opcode. MOP_PLAIN is
// the plain op of any op (a pseudo-op, below $20, as it is).
#define MOP_VOLATILE       1
#define MOP_IS_VOLATILE(op) ((op) >= 0x20 && ((op) & 1))
#define MOP_PLAIN(op)      ((op) >= 0x20 ? (op) & ~MOP_VOLATILE : (op))
// An op that only computes, a comparison (ISEQ to ISLE) or arithmetic
// (NOT to IDXW): no memory, no control, no volatile form.
#define MOP_IS_ARITH(op)   (((op) >= OP_ISEQ && (op) <= OP_ISLE) \
                            || ((op) >= OP_NOT && (op) <= OP_IDXW))
// ANDI for AND, ORI for IOR: the two pairs are the same distance apart.
#define MOP_BYTE_FORM(op)  ((op) - (OP_AND - OP_ANDI))
#define MOP_CN(v)          (OP_CN + 2 * (v))     // CN v, for v 0-15
#define MOP_CN_VALUE(op)   ((op) >> 1)           // CN's v
_Static_assert(MOP_CN_VALUE(MOP_CN(0)) == 0 && MOP_CN_VALUE(MOP_CN(15)) == 15,
               "MOP_CN_VALUE must undo MOP_CN");

// Is op a constant push (CN, MINUS1, CB, CW, CFFB), as
// mop_const_value() (lower.c) takes? Most ops are above CW, and are
// told by one compare and one test, so test this before calling it.
// It names op several times: pass it a local.
#define MOP_IS_CONST(op) ((op) > OP_CW ? (op) == OP_CFFB \
                          : ((op) & ~0x1E) == 0 || (op) == OP_MINUS1 \
                            || (((op) - OP_CB) & ~2) == 0)
_Static_assert(MOP_IS_CONST(MOP_CN(0)) && MOP_IS_CONST(MOP_CN(15))
               && MOP_IS_CONST(OP_MINUS1) && MOP_IS_CONST(OP_CB)
               && MOP_IS_CONST(OP_CW) && MOP_IS_CONST(OP_CFFB),
               "MOP_IS_CONST misses a member");
_Static_assert(!MOP_IS_CONST(OP_BREQ) && !MOP_IS_CONST(OP_LA)
               && !MOP_IS_CONST(OP_LLA) && !MOP_IS_CONST(OP_CS)
               && !MOP_IS_CONST(OP_DROP) && !MOP_IS_CONST(OP_RET)
               && !MOP_IS_CONST(OP_LB) && !MOP_IS_CONST(OP_LLW)
               && !MOP_IS_CONST(OP_ADDI) && !MOP_IS_CONST(1)
               && !MOP_IS_CONST(0x1F)
               && !MOP_IS_CONST(OP_CB | MOP_VOLATILE)
               && !MOP_IS_CONST(OP_CFFB | MOP_VOLATILE),
               "MOP_IS_CONST holds a non-member");

// For a branch op (BRNCH, BRFLS, ...), Mop.a is the label number; stage
// 4 turns it into a relative offset.

// What an operand word refers to, if it needs relocation: a data
// offset, a function, or an offset in the string pool.
enum { FIX_NONE, FIX_DATA, FIX_FUNC, FIX_STR, FIX_DSYM, FIX_HELPER,
       FIX_HEAP };    // FIX_DSYM and FIX_HEAP are the two operand
                      // words ccrel may rewrite (the fixups in emit.c);
                      // FIX_HEAP is IR_HEAPADDR's symbol, never folded

// Marks an ENTER/LEAVE frame size that is filled in at function end.
#define FRAME_LATER (-1)

// A frame operand (LLW, SLW, ...) for one of the back end's own two
// temporaries, k = 0 or 1, which stage 4 places past the parser's
// frame when the function ends (IR_SWAP's run-time exchange).
#define FRAME_TEMP(k) (-2 - (k))

// A machine op: 8 bytes in ccish (four words), so that indexing the
// peephole window is a shift, not a multiply.
typedef struct {
  int op;
  int a;          // first operand (byte or word), or data offset
  int fix;        // FIX_* for a word operand
  int fn;         // FIX_FUNC target; FIX_DSYM data symbol (global
                  // variable), with a the addend; OP_FUNC_BEGIN's
                  // function; the second operand of ENTER (its
                  // parameter count) and of OP_JOIN (the label to go to)
} Mop;

#endif
