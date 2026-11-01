/*
 * ccish's IR file, NAME.ir, which cccomp writes and ccgen reads
 * (approach.md 18.7; ../IR.md, ../OBJECT.md): its ops, records and
 * limits.
 */
#ifndef IR_H
#define IR_H

#define MAX_LABELS   104    /* branch targets in one function (90); at
                             * most 128, since case_label[] is char */
#define MAX_BYVAL    8      /* bytes in a struct passed or returned by
                             * value: at most MAX_BYVAL / 2 words, so a
                             * RETURN's a (below) */

/* ---------- Stage 1 -> 2: IR ops, through NAME.ir ----------
 * Each op's meaning, and why it differs from PLASMA's, is in ../IR.md;
 * the file's format is in ../OBJECT.md. The op numbers are part of the
 * format: changing them changes IR_VERSION. */

enum {
  IR_FUNC,        /* f: start a function */
  IR_ENDFUNC,     /* a = frame size: end the current function */
  IR_CONST,       /* a = value: push a constant */
  IR_DATAADDR,    /* a = data offset: push the address of data */
  IR_STRADDR,     /* a = string pool offset: push a string's address */
  IR_LOCAL,       /* a = frame offset: push a local word */
  IR_SETLOCAL,    /* a = frame offset: store top value there, keep it */
  IR_LOCALB,      /* a = frame offset: push a local byte (a char) */
  IR_SETLOCALB,   /* a = frame offset: store the top value's low byte
                   * there, and keep that byte as the value */
  IR_LOCALADDR,   /* a = frame offset: push the address of a local */
  IR_GLOBAL,      /* a = data offset: push a global word */
  IR_SETGLOBAL,   /* a = data offset: store top value there, keep it */
  IR_GLOBALB,     /* a = data offset: push a global byte (a char) */
  IR_SETGLOBALB,  /* a = data offset: store the top value's low byte
                   * there, and keep that byte as the value */
  IR_LOAD,        /* ( addr -- word ) load a word */
  IR_LOADB,       /* ( addr -- byte ) load a byte (a char) */
  IR_STORE,       /* ( val addr -- ) store a word */
  IR_STOREB,      /* ( val addr -- ) store val's low byte */
  IR_CALL,        /* f, a = argument count: call, push the result */
  IR_DROP,        /* discard the top value */
  IR_DUP,         /* duplicate the top value */
  IR_RETURN,      /* a = the result's words (0 for void, a struct's
                   * words, else 1): return them from the current
                   * function */
  /* binary: ( x y -- x op y ) */
  IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
  IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SHR,
  /* comparisons, binary, giving C's 1 or 0 (not PLASMA's $FFFF). LT,
   * GT and LE, GE stay adjacent pairs, IR_LT even: irin.c reverses one
   * by flipping bit 0. */
  IR_EQ, IR_NE, IR_LT, IR_GT, IR_LE, IR_GE,
  /* unary: ( x -- op x ) */
  IR_NEG, IR_COMP,
  IR_NOT,         /* C's !: 1 if x is 0, else 0 (not PLASMA's NOT) */
  /* control flow; a = label number, local to the function */
  IR_LABEL,       /* a branch target */
  IR_JUMP,        /* always branch */
  IR_BRFALSE,     /* ( x -- ) branch if x is 0 */
  IR_BRTRUE,      /* ( x -- ) branch if x is not 0 */
  IR_JOIN,        /* the branches so far to label a go to label f */
  IR_SEL,         /* ( x -- ) go to the case table at label a (a switch) */
  /* a case table, after its label: */
  IR_CASES,       /* a = the number of cases that follow */
  IR_CASEVAL,     /* a = one case's value; then: */
  IR_CASETO,      /* a = the label of that case's code */
  /* reordering (approach.md 18.7, A), done by ccgen: */
  IR_SWAP,        /* a = the top block's length in IR ops, f = the
                   * block below's, or -1 if code must not move:
                   * exchange the values the two blocks left */
  /* unsigned comparisons (18.7, C), in the order of LT GT LE GE; a =
   * the right operand's block length in IR ops, or 0 if not known */
  IR_ULT, IR_UGT, IR_ULE, IR_UGE,
  /* unsigned arithmetic PLASMA lacks, binary ( x y -- x op y ), for
   * unsigned operands; ccgen lowers them (approach.md, "unsigned and
   * helper functions design") */
  IR_UDIV, IR_UMOD, IR_USHR,
  /* f = data symbol: push the address of a global that may move to
   * the heap (approach.md, "Large zero-filled data on the heap"):
   * always an LA of the symbol itself, which ccrel may turn into an
   * LAW of its pointer word, so never folded into another op */
  IR_HEAPADDR,
  /* loop markers (approach.md, "Loop design"), in source order around
   * a loop's parts; ccgen writes the loop's own jumps and labels, and
   * moves the test and step after the body. They must stay last. */
  IR_LOOP,        /* a = the test's label, f = the break label: the
                   * test follows, branching to the body when true */
  IR_LOOPSTEP,    /* a = the continue label, f = the break label: the
                   * step follows (none in a while loop) */
  IR_LOOPBODY,    /* a = the body's label, f = the test's: the body
                   * follows */
  IR_LOOPEND,     /* a = the continue label, f = the break label */
  IR_NOPS,        /* the number of IR ops */
  /* not in the IR file: ccgen's own, for a counted loop's branches
   * (irin.c), which have no IR op: machine op a, with label f */
  IR_MOP = IR_NOPS
};

/* The IR file: the output name plus this suffix, and its format's
 * version (in its header, after the magic "CIR"). */
#define SUFFIX_IR   ".ir"
#define IR_VERSION  6

/* Its record tags besides the stream's D, R and S (below), and the bit
 * that marks an IR op followed by a one-byte operand. An IR op's tag is
 * its number, below IR_TAG_END. */
enum { REC_LINE = 'L', REC_NAME = 'N', REC_WORD = 'W', REC_SECOND = 'X',
       REC_SUMMARY = 'Y' };
#define IR_BYTE_OP  0x80
#define IR_TAG_END  0x40

/* A name record's flags byte: NAME_RETURNS set if the function returns
 * a value. */
#define NAME_RETURNS 0x80

#endif
