/*
 * ccgen's machine ops (approach.md, "Stage 2 -> 3 -> 4"): PLASMA's
 * opcodes and ccgen's pseudo-ops, as stages 2 to 4 (lower.c, peep.c,
 * emit.c) pass them along.
 */
#ifndef MOP_H
#define MOP_H

/* PLASMA opcodes used so far (see ../../doc/Bytecodes.md). */
enum {
  OP_CN = 0x00, OP_MINUS1 = 0x20, OP_BREQ = 0x22, OP_BRNE = 0x24,
  OP_LA = 0x26, OP_LLA = 0x28, OP_CB = 0x2A,
  OP_CW = 0x2C, OP_DROP = 0x30, OP_DUP = 0x34, OP_ADDI = 0x38,
  OP_SUBI = 0x3A, OP_ANDI = 0x3C,
  OP_ISEQ = 0x40, OP_ISNE = 0x42, OP_ISGT = 0x44, OP_ISLT = 0x46,
  OP_ISGE = 0x48, OP_ISLE = 0x4A, OP_BRFLS = 0x4C, OP_BRTRU = 0x4E,
  OP_BRNCH = 0x50, OP_SEL = 0x52,
  OP_CALL = 0x54, OP_ENTER = 0x58, OP_LEAVE = 0x5A, OP_RET = 0x5C,
  OP_CFFB = 0x5E, OP_LB = 0x60, OP_LW = 0x62, OP_LLB = 0x64,
  OP_LLW = 0x66, OP_LAB = 0x68, OP_LAW = 0x6A, OP_DLB = 0x6C,
  OP_DLW = 0x6E, OP_SB = 0x70, OP_SW = 0x72, OP_SLB = 0x74, OP_SLW = 0x76,
  OP_SAB = 0x78, OP_SAW = 0x7A, OP_DAB = 0x7C, OP_DAW = 0x7E,
  OP_NOT = 0x80, OP_ADD = 0x82, OP_SUB = 0x84, OP_MUL = 0x86, OP_DIV = 0x88,
  OP_MOD = 0x8A, OP_INCR = 0x8C, OP_DECR = 0x8E, OP_NEG = 0x90,
  OP_COMP = 0x92, OP_AND = 0x94, OP_IOR = 0x96, OP_XOR = 0x98,
  OP_SHL = 0x9A, OP_SHR = 0x9C, OP_IDXW = 0x9E, OP_ADDLB = 0xB0, OP_ADDLW = 0xB2, OP_ADDAB = 0xB4,
  OP_ADDAW = 0xB6, OP_IDXLB = 0xB8, OP_IDXLW = 0xBA, OP_IDXAB = 0xBC,
  OP_IDXAW = 0xBE,
  /* Pseudo-ops: markers that travel through stages 2-4 in order with
   * the code, but emit no bytes themselves. */
  OP_FUNC_BEGIN = 0x100,  /* fn: a function starts here */
  OP_FUNC_END = 0x101,    /* a = frame size: the function ends here */
  OP_LABEL = 0x102,       /* a = label number: a branch target is here */
  /* A SEL's case table (see ../../doc/Bytecodes.md), as data in the
   * code: */
  OP_CASES = 0x103,       /* a = the count byte */
  OP_CASEVAL = 0x104,     /* a = a case's value word */
  OP_CASETO = 0x105,      /* a = the label its offset word leads to */
  OP_JOIN = 0x106         /* the branches so far to label a go to
                           * label fn instead */
};

/* For a branch op (BRNCH, BRFLS, ...), Mop.a is the label number; stage
 * 4 turns it into a relative offset. */

/* What an operand word refers to, if it needs relocation: a data
 * offset, a function, or an offset in the string pool. */
enum { FIX_NONE, FIX_DATA, FIX_FUNC, FIX_STR, FIX_DSYM, FIX_HELPER };

/* Marks an ENTER/LEAVE frame size that is filled in at function end. */
#define FRAME_LATER (-1)

/* A frame operand (LLW, SLW, ...) for one of the back end's own two
 * temporaries, k = 0 or 1, which stage 4 places past the parser's
 * frame when the function ends (IR_SWAP's run-time exchange). */
#define FRAME_TEMP(k) (-2 - (k))

/* A machine op: 8 bytes in ccish (four words), so that indexing the
 * peephole window is a shift, not a multiply. */
typedef struct {
  int op;
  int a;          /* first operand (byte or word), or data offset */
  int fix;        /* FIX_* for a word operand */
  int fn;         /* FIX_FUNC target; FIX_DSYM data symbol (global
                   * variable), with a the addend; OP_FUNC_BEGIN's
                   * function; the second operand of ENTER (its
                   * parameter count) and of OP_JOIN (the label to go to) */
} Mop;

#endif
