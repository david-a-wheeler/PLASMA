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
 * until the function has too many of one. A counted loop runs its body
 * with its limit and count on the stack under it, so, on purpose, its
 * body may hold neither a goto nor a goto label: a forward goto into
 * the body lands where the stack's depth is unknown, and a goto out of
 * it skips the loop's drops (body_ok, irin.c). Such a loop stays
 * uncounted (the plain form) and its gotos run on a clean stack. */
#define GOTO_LABEL_BASE (ENTRY_LABEL + 1)
#define GOTO_LABELS (MAX_LABELS - GOTO_LABEL_BASE)
#define IS_GOTO_LABEL(n) ((n) >= GOTO_LABEL_BASE && (n) < MAX_LABELS)
#define MAX_BYVAL    8      /* bytes in a struct passed or returned by
                             * value: at most MAX_BYVAL / 2 words, so a
                             * RETURN's a (below) */

/* ---------- Stage 1 -> 2: IR ops, through NAME.ir ----------
 * Each op's meaning, and why it differs from PLASMA's, is in ../IR.md;
 * the file's format is in ../OBJECT.md. The op numbers are part of the
 * format: changing them changes IR_VERSION. They are grouped in blocks
 * whose bits mean something, so that one op is made from another with
 * a bit operation; each such operation is a macro below (IR_ACC_BYTE
 * and the rest), so a change to the numbers shows what it touches. The
 * tables indexed by op (irin.c's stk_effect[], lower.c's lw_kind[],
 * lw_op[] and ir_names[]) follow the numbers too. */

enum {
  /* The access ops, $00-$0B: a variable's or an address's value, and
   * stores to it. Their bits mean something (IR_ACC_BYTE and the rest,
   * below): bit 0 a byte (a char) rather than a word, bit 1 a store
   * rather than a load, bits 2-3 where (0 the frame, 1 the data, 2
   * through an address on the stack). $20-$2F are kept for their
   * volatile forms (approach.md issue 25). */
  IR_LOCAL = 0x00,  /* a = frame offset: push a local word */
  IR_LOCALB,      /* a = frame offset: push a local byte (a char) */
  IR_SETLOCAL,    /* a = frame offset: store top value there, keep it */
  IR_SETLOCALB,   /* a = frame offset: store the top value's low byte
                   * there, and keep that byte as the value */
  IR_GLOBAL,      /* a = data offset: push a global word */
  IR_GLOBALB,     /* a = data offset: push a global byte (a char) */
  IR_SETGLOBAL,   /* a = data offset: store top value there, keep it */
  IR_SETGLOBALB,  /* a = data offset: store the top value's low byte
                   * there, and keep that byte as the value */
  IR_LOAD,        /* ( addr -- word ) load a word */
  IR_LOADB,       /* ( addr -- byte ) load a byte (a char) */
  IR_STORE,       /* ( val addr -- ) store a word */
  IR_STOREB,      /* ( val addr -- ) store val's low byte */
  /* addresses, $0C-$0F: */
  IR_LOCALADDR,   /* a = frame offset: push the address of a local */
  IR_DATAADDR,    /* a = data offset: push the address of data */
  /* f = data symbol: push the address of a global that may move to
   * the heap (approach.md, "Large zero-filled data on the heap"):
   * always an LA of the symbol itself, which ccrel may turn into an
   * LAW of its pointer word, so never folded into another op */
  IR_HEAPADDR,
  IR_STRADDR,     /* a = string pool offset: push a string's address */
  /* $10-$1F: */
  IR_FUNC = 0x10, /* f: start a function */
  IR_ENDFUNC,     /* a = frame size: end the current function */
  IR_CONST,       /* a = value: push a constant */
  IR_CALL,        /* f, a = argument count: call, push the result */
  IR_DROP,        /* discard the top value */
  IR_DUP,         /* duplicate the top value */
  IR_RETURN,      /* a = the result's words (0 for void, a struct's
                   * words, else 1): return them from the current
                   * function */
  /* function pointers (approach.md, "Function pointer design"): */
  IR_FUNCADDR,    /* f = function: push its address */
  IR_ICALL,       /* a = argument count, f = the result's words:
                   * ( args addr -- result ) call the function at
                   * addr */
  /* unary: ( x -- op x ) */
  IR_NEG, IR_COMP,
  IR_NOT,         /* C's !: 1 if x is 0, else 0 (not PLASMA's NOT) */
  /* reordering (approach.md 18.7, A), done by ccgen: */
  IR_SWAP,        /* a = the top block's length in IR ops, f = the
                   * block below's, or -1 if code must not move:
                   * exchange the values the two blocks left */
  /* unsigned arithmetic PLASMA lacks, binary ( x y -- x op y ), for
   * unsigned operands; ccgen lowers them (approach.md, "unsigned and
   * helper functions design") */
  IR_UDIV, IR_UMOD, IR_USHR,
  /* The volatile access ops, $20-$2B (no names of their own here, so
   * that they cost cccomp no MAX_IDS entries): each access op, in the
   * same order, | IR_VOLATILE (IR_FORM, below), for an object whose
   * type is volatile (approach.md issue 25). Each does what its plain
   * form does, but no optimization that would change how often it
   * runs, or merge it, matches it: a pattern names the ops it
   * rewrites, and these aren't those. $2C-$2F, an address op |
   * IR_VOLATILE, are no op. */
  /* binary, $30-$39: ( x y -- x op y ) */
  IR_ADD = 0x30, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
  IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SHR,
  /* comparisons, $40-$49, binary, giving C's 1 or 0 (not PLASMA's
   * $FFFF): all in one range (IR_IS_COMPARE), in adjacent De Morgan
   * pairs from an even number, so the opposite of any is its number
   * with bit 0 flipped (IR_COMPARE_FLIP): EQ NE, LT GE, GT LE, then the
   * unsigned (18.7, C) in the same order, at IR_U_COMPARE() of them;
   * the unsigned's a = the right operand's block length in IR ops, or
   * 0 if not known */
  IR_EQ = 0x40, IR_NE, IR_LT, IR_GE, IR_GT, IR_LE,
  IR_ULT, IR_UGE, IR_UGT, IR_ULE,
  /* control flow, $50-$58; a = label number, local to the function.
   * JUMP through SEL are the ops that name a label in a
   * (IR_REFS_LABEL), JOIN behind them; BRFALSE is even and BRTRUE
   * the next (IR_BR_IF) */
  IR_LABEL = 0x50,  /* a branch target */
  IR_JUMP,        /* always branch */
  IR_BRFALSE,     /* ( x -- ) branch if x is 0 */
  IR_BRTRUE,      /* ( x -- ) branch if x is not 0 */
  IR_SEL,         /* ( x -- ) go to the case table at label a (a switch) */
  IR_JOIN,        /* the branches so far to label a go to label f */
  /* a case table, after its label: */
  IR_CASES,       /* a = the number of cases that follow */
  IR_CASEVAL,     /* a = one case's value; then: */
  IR_CASETO,      /* a = the label of that case's code */
  /* loop markers (approach.md, "Loop design"), in source order around
   * a loop's parts; ccgen writes the loop's own jumps and labels, and
   * moves the test and step after the body. They must stay last. */
  IR_LOOP = 0x5C, /* a = the test's label, f = the break label: the
                   * test follows, branching to the body when true */
  IR_LOOPSTEP,    /* a = the continue label, f = the break label: the
                   * step follows (none in a while loop) */
  IR_LOOPBODY,    /* a = the body's label, f = the test's: the body
                   * follows */
  IR_LOOPEND,     /* a = the continue label, f = the break label */
  IR_NOPS,        /* past the highest IR op: at most IR_TAG_END (96),
                   * since an op's tag is a byte below it (below). The
                   * numbers not named here are no op: the reader
                   * refuses them (irin.c, stk_effect[]) */
  /* not in the IR file: ccgen's own, for a counted loop's branches
   * (irin.c), which have no IR op: machine op a, with label f */
  IR_MOP = IR_NOPS
};

/* The IR file: the output name plus this suffix, and its format's
 * version (in its header, after the magic "CIR"). */
#define SUFFIX_IR   ".ir"
#define IR_VERSION  14

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
/* Is op (0 or more) a number some IR op has? The gaps between the
 * blocks are none, and the reader refuses them (irin.c). */
#define IR_IS_OP(op)  ((op) <= (IR_STOREB | IR_VOLATILE) || IR_IS_BINARY(op) \
                       || IR_IS_COMPARE(op) \
                       || ((op) >= IR_LABEL && (op) <= IR_CASETO) \
                       || IR_IS_LOOP_MARKER(op))
/* An unsigned comparison, from the signed one of the same sense (LT
 * to ULT, and so on); valid only on IR_IS_RELATIONAL. */
#define IR_U_COMPARE(op)     ((op) + (IR_ULT - IR_LT))
/* BRTRUE if t, else BRFALSE; t must be 0 or 1 (BRTRUE is BRFALSE | 1). */
#define IR_BR_IF(t)          (IR_BRFALSE | (t))
/* The access ops' bits (IR_LOCAL ... IR_STOREB, and their volatile
 * forms), valid only on them: IR_IS_ACCESS. The char form of a word
 * access, if c (which must be 0 or 1, as IR_ACC_BYTE is 1); a load's
 * store, which keeps its value for LOCAL and GLOBAL (SETLOCAL), not
 * for LOAD (STORE); the volatile form, and the plain form of either.
 * IR_FORM adds form, a mix of IR_ACC_BYTE and IR_VOLATILE, to an
 * access op. (Q_VOLATILE, in type.h, is IR_VOLATILE << 4: parse.c's
 * acc_form() relies on it.) */
#define IR_ACC_BYTE          0x01
#define IR_ACC_STORE         0x02
#define IR_VOLATILE          0x20
#define IR_IS_ACCESS(op)     (((op) & ~IR_VOLATILE) < IR_LOCALADDR)
#define IR_IS_VOLATILE(op)   ((op) >= (IR_LOCAL | IR_VOLATILE) \
                              && (op) <= (IR_STOREB | IR_VOLATILE))
#define IR_CHAR_FORM(op, c)  ((op) | (c))
#define IR_STORE_FORM(op)    ((op) | IR_ACC_STORE)
#define IR_PLAIN_FORM(op)    ((op) & ~IR_VOLATILE)
#define IR_FORM(op, form)    ((op) | (form))

/* The bit that marks an IR op followed by a one-byte operand. An IR
 * op's tag is its number, below IR_TAG_END ($60), so up to 96 ops; a
 * record's tag is its letter (obj.fmt.h) in lowercase, IR_REC(), $61
 * to $7A, so that the ops can have the numbers the uppercase letters
 * would take. ccgen copies a data record to the stream file with its
 * uppercase letter. */
#define IR_BYTE_OP  0x80
#define IR_TAG_END  0x60
#define IR_REC_CASE 0x20
#define IR_REC(rec) ((rec) | IR_REC_CASE)

/* A name record's flags byte: NAME_RETURNS set if the function returns
 * a value. */
#define NAME_RETURNS 0x80

#endif
