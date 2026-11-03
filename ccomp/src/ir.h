/*
 * ccish's IR file, NAME.ir, which cccomp writes and ccgen reads
 * (approach.md 18.7; ../IR.md, ../OBJECT.md): its ops, records and
 * limits.
 */
#ifndef IR_H
#define IR_H

#define MAX_LABELS   128    /* branch targets in one function (90); at
                             * most 128, since case_label[] is char */
/* The function's entry label, after its ENTER: ccgen's target for a
 * self tail call's jump (decision 33); ccgen holds it at the
 * function's start, and the front end's ordinary labels stop below it.
 * It is fixed at 103 (not MAX_LABELS - 1) so the goto block can grow
 * above it without moving it (decision 34). */
#define ENTRY_LABEL    103
/* Goto labels (decision 34): a label is a goto's target iff it is in
 * [GOTO_LABEL_BASE, MAX_LABELS), the block above the entry label. The
 * front end allocates goto labels from GOTO_LABEL_BASE up and ordinary
 * labels from 0 up (stopping at ENTRY_LABEL), so the two never meet
 * until the function has too many of one. A counted loop keeps its
 * limit and count on the stack under its body, so its body may hold
 * neither a goto (a jump to a goto label) nor a goto label: ccgen's
 * body_ok keeps such loops uncounted, and every goto then runs on a
 * clean stack and is just an unconditional branch. */
#define GOTO_LABEL_BASE (ENTRY_LABEL + 1)
#define GOTO_LABELS (MAX_LABELS - GOTO_LABEL_BASE)
#define IS_GOTO_LABEL(n) ((n) >= GOTO_LABEL_BASE && (n) < MAX_LABELS)
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
  /* comparisons, binary, giving C's 1 or 0 (not PLASMA's $FFFF): all
   * in one range (IR_IS_COMPARE), in adjacent De Morgan pairs, so the
   * opposite of any is its number with bit 0 flipped
   * (IR_COMPARE_FLIP): EQ NE, LT GE, GT LE, then the unsigned (18.7,
   * C) in the same order at the offset IR_ULT - IR_LT from them
   * (parse.c adds that offset to find one); the unsigned's a = the
   * right operand's block length in IR ops, or 0 if not known */
  IR_EQ, IR_NE, IR_LT, IR_GE, IR_GT, IR_LE,
  IR_ULT, IR_UGE, IR_UGT, IR_ULE,
  /* unary: ( x -- op x ) */
  IR_NEG, IR_COMP,
  IR_NOT,         /* C's !: 1 if x is 0, else 0 (not PLASMA's NOT) */
  /* control flow; a = label number, local to the function. JUMP
   * through SEL are the ops that name a label in a (IR_REFS_LABEL),
   * JOIN behind them */
  IR_LABEL,       /* a branch target */
  IR_JUMP,        /* always branch */
  IR_BRFALSE,     /* ( x -- ) branch if x is 0 */
  IR_BRTRUE,      /* ( x -- ) branch if x is not 0 */
  IR_SEL,         /* ( x -- ) go to the case table at label a (a switch) */
  IR_JOIN,        /* the branches so far to label a go to label f */
  /* a case table, after its label: */
  IR_CASES,       /* a = the number of cases that follow */
  IR_CASEVAL,     /* a = one case's value; then: */
  IR_CASETO,      /* a = the label of that case's code */
  /* reordering (approach.md 18.7, A), done by ccgen: */
  IR_SWAP,        /* a = the top block's length in IR ops, f = the
                   * block below's, or -1 if code must not move:
                   * exchange the values the two blocks left */
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
  IR_NOPS,        /* the number of IR ops: at most 64, since an op's
                   * tag is a byte below IR_TAG_END (below) */
  /* not in the IR file: ccgen's own, for a counted loop's branches
   * (irin.c), which have no IR op: machine op a, with label f */
  IR_MOP = IR_NOPS
};

/* The IR file: the output name plus this suffix, and its format's
 * version (in its header, after the magic "CIR"). */
#define SUFFIX_IR   ".ir"
#define IR_VERSION  8

/* The sets of ops the compiler tests in more than one place, and its
 * value manipulations on ops, each defined here next to the values it
 * names, so all such tests and manipulations are in one place
 * (approach.md, decision 26, amended); changing the order changes
 * IR_VERSION, and the constraints on it are in the comments at the
 * ops. */
#define IR_IS_BINARY(op)     ((op) >= IR_ADD && (op) <= IR_SHR)
#define IR_IS_COMPARE(op)    ((op) >= IR_EQ && (op) <= IR_ULE)  /* both signs */
#define IR_IS_S_COMPARE(op)  ((op) >= IR_EQ && (op) <= IR_LE)   /* the signed half */
#define IR_IS_RELATIONAL(op) ((op) >= IR_LT && (op) <= IR_LE)   /* signed < > <= >= */
/* The opposite comparison, the one ! gives (De Morgan): EQ <-> NE,
 * LT <-> GE, GT <-> LE, and the unsigned. The pairs are adjacent in
 * the order above, so it is bit 0; it is valid only on an
 * IR_IS_COMPARE op. (Exchanging the operands of a relational keeps its
 * sense for LT <-> GT and LE <-> GE: that is irin.c's mirror, not
 * this.) */
#define IR_COMPARE_FLIP(op)  ((op) ^ 1)
#define IR_IS_DIVMOD(op)     ((op) >= IR_DIV && (op) <= IR_MOD)
#define IR_IS_S_SHIFT(op)    ((op) >= IR_SHL && (op) <= IR_SHR)
#define IR_IS_U_COMPARE(op)  ((op) >= IR_ULT && (op) <= IR_ULE)
#define IR_IS_U_ARITH(op)    ((op) >= IR_UDIV && (op) <= IR_USHR)
#define IR_IS_BRANCH(op)     ((op) >= IR_JUMP && (op) <= IR_BRTRUE)
#define IR_REFS_LABEL(op)    (((op) >= IR_JUMP && (op) <= IR_SEL) \
                              || (op) == IR_CASETO)
#define IR_IS_LOOP_MARKER(op) ((op) >= IR_LOOP && (op) <= IR_LOOPEND)

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
