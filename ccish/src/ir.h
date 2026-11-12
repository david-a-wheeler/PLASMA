// ccish's IR file, NAME.ir, which cccomp writes and ccgen reads
// (approach.md 18.7; ../IR.md, ../OBJECT.md): its ops, records and
// limits.
#ifndef IR_H
#define IR_H

// ENUM VALUES: the values below are assigned for the set tests and
// value manipulations on them (the #defines here, and file-local
// ones in the .c files, each with a _Static_assert of what it
// relies on), and may be reassigned freely as long as those checks
// hold; never test or transform one with a raw number outside such
// a define and assert pair. See ../doc/enums.md.

#define MAX_LABELS   128    // limit function: labels in one function,
                            // ordinary and goto; at most 128, since
                            // case_label[] is char. Each if, loop, &&, || and
                            // ?: uses one to four
// The function's entry label, after its ENTER: ccgen's target for a
// self tail call's jump (decision 33); ccgen holds it at the
// function's start, and the front end's ordinary labels stop below it.
// It is fixed at 103 (not MAX_LABELS - 1) so the goto block can grow
// above it without moving it (decision 34).
#define ENTRY_LABEL    103  // limit function: ordinary labels in one
                            // function: the goto labels are the rest of
                            // MAX_LABELS, above this one
// Goto labels (decision 34): a label is a goto's target iff it is in
// [GOTO_LABEL_BASE, MAX_LABELS), the block above the entry label. The
// front end allocates goto labels from GOTO_LABEL_BASE up and ordinary
// labels from 0 up (stopping at ENTRY_LABEL), so the two never meet
// until the function has too many of one. A counted loop runs its body
// with its limit and count on the stack under it, so, on purpose, its
// body may hold neither a goto nor a goto label: a forward goto into
// the body lands where the stack's depth is unknown, and a goto out of
// it skips the loop's drops (body_ok, irin.c). Such a loop stays
// uncounted (the plain form) and its gotos run on a clean stack.
#define GOTO_LABEL_BASE (ENTRY_LABEL + 1)
#define GOTO_LABELS (MAX_LABELS - GOTO_LABEL_BASE)
#define IS_GOTO_LABEL(n) ((n) >= GOTO_LABEL_BASE && (n) < MAX_LABELS)
#define MAX_BYVAL    8      // limit other: bytes in a struct passed or
                            // returned by value, so IR_RETURN's a (the
                            // result's words) is at most MAX_BYVAL / 2

// ---------- Stage 1 -> 2: IR ops, through NAME.ir ----------
// Each op's meaning, and why it differs from PLASMA's, is in ../IR.md;
// the file's format is in ../OBJECT.md. The op numbers are part of the
// format: changing them changes IR_VERSION. They are grouped in blocks
// whose bits mean something, so that one op is made from another with
// a bit operation, and a set the compiler tests is one block: a power
// of 2 in size, starting at a multiple of its size, so that ccgen tests
// it with one AND and one compare (irin.c's between_rewrite()). Each
// such set and operation is a macro below, with checks of what it
// relies on, so the numbers may be reassigned freely as long as those
// hold. A block's spare numbers are marked UNUSED: room for a new op of
// its kind, and no op (the reader refuses them: irin.c, stk_effect[]).
// They aren't names, since each name costs cccomp an entry in its table
// of enum constants (MAX_IDS). The tables indexed by op (irin.c's
// stk_effect[], lower.c's lw_kind[], lw_op[] and ir_names[]) follow the
// numbers; tests/special/tables.c checks them.

enum {
  // The access ops, $00-$0B: a variable's or an address's value, and
  // stores to it. Their bits mean something (IR_ACC_BYTE and the rest,
  // below): bit 0 a byte (a char) rather than a word, bit 1 a store
  // rather than a load, bits 2-3 where (0 the frame, 1 the data, 2
  // through an address on the stack). $20-$2B are their volatile forms
  // (approach.md issue 25).
  IR_LOCAL = 0x00,  // a = frame offset: push a local word
  IR_LOCALB,      // a = frame offset: push a local byte (a char)
  IR_SETLOCAL,    // a = frame offset: store top value there, keep it
  IR_SETLOCALB,   // a = frame offset: store the top value's low byte
                  // there, and keep that byte as the value
  IR_GLOBAL,      // a = data offset: push a global word
  IR_GLOBALB,     // a = data offset: push a global byte (a char)
  IR_SETGLOBAL,   // a = data offset: store top value there, keep it
  IR_SETGLOBALB,  // a = data offset: store the top value's low byte
                  // there, and keep that byte as the value
  IR_LOAD,        // ( addr -- word ) load a word
  IR_LOADB,       // ( addr -- byte ) load a byte (a char)
  IR_STORE,       // ( val addr -- ) store a word
  IR_STOREB,      // ( val addr -- ) store val's low byte
  // addresses, $0C-$0F:
  IR_LOCALADDR,   // a = frame offset: push the address of a local
  IR_DATAADDR,    // a = data offset: push the address of data
  // f = data symbol: push the address of a global that may move to
  // the heap (approach.md, "Large zero-filled data on the heap"):
  // always an LA of the symbol itself, which ccrel may turn into an
  // LAW of its pointer word, so never folded into another op
  IR_HEAPADDR,
  IR_STRADDR,     // a = string pool offset: push a string's address
  // $10-$13, the ops whose f is a function (IR_NAMES_FUNC):
  IR_FUNC = 0x10, // f: start a function
  IR_CALL,        // f, a = argument count: call, push the result
  // function pointers (approach.md, "Function pointer design"):
  IR_FUNCADDR,    // f = function: push its address
                  // $13: UNUSED (IR_NAMES_FUNC's block of 4)
  // $14-$1F:
  IR_ENDFUNC = IR_FUNC + 4, // a = frame size: end the current function
  IR_CONST,       // a = value: push a constant
  IR_DROP,        // discard the top value
  IR_DUP,         // duplicate the top value
  IR_RETURN,      // a = the result's words (0 for void, a struct's
                  // words, else 1): return them from the current
                  // function
  IR_ICALL,       // a = argument count, f = the result's words:
                  // ( args addr -- result ) call the function at
                  // addr
  // reordering (approach.md 18.7, A), done by ccgen:
  IR_SWAP,        // a = the top block's length in IR ops, f = the
                  // block below's, or -1 if code must not move:
                  // exchange the values the two blocks left
  IR_REGLOCAL,    // a = frame offset: the local there is register, so
                  // no pointer ever reaches it (C11 6.5.3.2p1); from
                  // here to the function's end
  // unary: ( x -- op x )
  IR_NEG = IR_FUNC + 0x0C, IR_COMP,
  IR_NOT,         // C's !: 1 if x is 0, else 0 (not PLASMA's NOT)
                  // $1F: UNUSED
  // The volatile access ops, $20-$2B (no names of their own here, so
  // that they cost cccomp no MAX_IDS entries): each access op, in the
  // same order, | IR_VOLATILE (IR_FORM, below), for an object whose
  // type is volatile (approach.md issue 25). Each does what its plain
  // form does, but no optimization that would change how often it
  // runs, or merge it, matches it: a pattern names the ops it
  // rewrites, and these aren't those.
  // $2C-$2F, unsigned arithmetic PLASMA lacks (IR_IS_U_ARITH), binary
  // ( x y -- x op y ), for unsigned operands; ccgen lowers them
  // (approach.md, "unsigned and helper functions design"). The numbers
  // an address op | IR_VOLATILE would have, which is no op.
  IR_UDIV = 0x2C, IR_UMOD, IR_USHR,
                  // $2F: UNUSED (IR_IS_U_ARITH's block of 4)
  // binary, $30-$3F (IR_IS_BINARY): ( x y -- x op y ). First the 8 whose
  // operands ccgen can exchange (IR_IS_SWAPPABLE: all commutative but
  // SUB, which becomes SUB NEG), ADD and SUB a pair (IR_IS_ADDSUB, and
  // ++ and --'s); then DIV and MOD, and the shifts, each a pair.
  IR_ADD = 0x30, IR_SUB, IR_MUL, IR_AND, IR_OR, IR_XOR,
                  // $36-$37: UNUSED (IR_IS_SWAPPABLE's block of 8)
  IR_DIV = IR_ADD + 8, IR_MOD, IR_SHL, IR_SHR,
                  // $3C-$3F: UNUSED (IR_IS_BINARY's block of 16)
  // comparisons, $40-$4F (IR_IS_COMPARE), binary, giving C's 1 or 0
  // (not PLASMA's $FFFF), in adjacent De Morgan pairs from an even
  // number, so the opposite of any is its number with bit 0 flipped
  // (IR_COMPARE_FLIP). First the signed (IR_IS_S_COMPARE, $40-$47): the
  // 4 relationals (IR_IS_RELATIONAL), LT GE GT LE, a block of 4, so the
  // mirror of each is bit 1 flipped (IR_COMPARE_MIRROR), then EQ NE,
  // the commutative pair; then the unsigned relationals (18.7, C), in
  // the same order, at IR_U_COMPARE() of them; the unsigned's a = 0
  IR_LT = 0x40, IR_GE, IR_GT, IR_LE, IR_EQ, IR_NE,
                  // $46-$47: UNUSED (IR_IS_S_COMPARE's block of 8)
  IR_ULT = IR_LT + 8, IR_UGE, IR_UGT, IR_ULE,
                  // $4C-$4F: UNUSED (IR_IS_COMPARE's block of 16)
  // control flow, $50-$5F; a = label number, local to the function.
  // $50-$57 are the ops that name a label in a (IR_REFS_LABEL): the
  // branches (IR_IS_BRANCH), with BRFALSE even and BRTRUE the next
  // (IR_BR_IF), then SEL and CASETO
  IR_JUMP = 0x50, // always branch
                  // $51: UNUSED (IR_IS_BRANCH's block of 4)
  IR_BRFALSE = IR_JUMP + 2, // ( x -- ) branch if x is 0
  IR_BRTRUE,      // ( x -- ) branch if x is not 0
  IR_SEL,         // ( x -- ) go to the case table at label a (a switch)
  IR_CASETO,      // a = the label of a case's code, after its CASEVAL
                  // $56-$57: UNUSED (IR_REFS_LABEL's block of 8)
  IR_LABEL = IR_JUMP + 8,  // a branch target
  IR_JOIN,        // the branches so far to label a go to label f
  // a case table, after its label:
  IR_CASES,       // a = the number of cases that follow
  IR_CASEVAL,     // a = one case's value; then a CASETO
  // loop markers (approach.md, "Loop design"), $5C-$5F
  // (IR_IS_LOOP_MARKER), in source order around a loop's parts; ccgen
  // writes the loop's own jumps and labels, and moves the test and step
  // after the body.
  IR_LOOP = IR_JUMP + 0x0C, // a = the test's label, f = the break label: the
                  // test follows, branching to the body when true
  IR_LOOPSTEP,    // a = the continue label, f = the break label: the
                  // step follows (none in a while loop)
  IR_LOOPBODY,    // a = the body's label, f = the test's: the body
                  // follows
  IR_LOOPEND,     // a = the continue label, f = the break label
  IR_NOPS,        // past the highest IR op: at most IR_TAG_END (96),
                  // since an op's tag is a byte below it (below)
  // not in the IR file: ccgen's own, for a counted loop's branches
  // (irin.c), which have no IR op: machine op a, with label f
  IR_MOP = IR_NOPS
};

// A global op's f operand (IR_DATAADDR, IR_GLOBAL and the rest) for an
// offset in the file's zero area, where its zero-filled static locals
// are (IR.md); -1 is the file's data, and 0 up a data symbol.
#define IR_ZERO_AREA (-2)

// The IR file: the output name plus this suffix, and its format's
// version (in its header, after the magic "CIR").
#define SUFFIX_IR   ".ir"
#define IR_VERSION  17

// The sets of ops the compiler tests, and its value manipulations on
// ops, each defined here next to the values it names, so all such
// tests and manipulations are in one place (approach.md, decision 26,
// amended). A set that's one block is IN_BLOCK(op, first, size):
// the block's first op and its size, a power of 2, with the block's
// first op a multiple of it (checked at each), which ccgen tests with
// one AND and one compare. Its UNUSED numbers are in it too, which is
// harmless: they never get past the reader. Each check says the set's
// members are in it, and the block's alignment and size; and each
// manipulation's, that it gives what it says.
#define IR_IS_BINARY(op)     IN_BLOCK(op, IR_ADD, 16)
#define IR_IS_SWAPPABLE(op)  IN_BLOCK(op, IR_ADD, 8)  // ADD ... XOR
#define IR_IS_ADDSUB(op)     IN_BLOCK(op, IR_ADD, 2)
#define IR_IS_DIVMOD(op)     IN_BLOCK(op, IR_DIV, 2)
#define IR_IS_S_SHIFT(op)    IN_BLOCK(op, IR_SHL, 2)
_Static_assert((IR_ADD & 15) == 0 && IR_SHR - IR_ADD < 16
               && IR_XOR - IR_ADD < 8 && IR_SUB == IR_ADD + 1
               && (IR_DIV & 1) == 0 && IR_MOD == IR_DIV + 1
               && (IR_SHL & 1) == 0 && IR_SHR == IR_SHL + 1
               && IR_IS_SWAPPABLE(IR_MUL) && IR_IS_SWAPPABLE(IR_AND)
               && IR_IS_SWAPPABLE(IR_OR) && !IR_IS_SWAPPABLE(IR_DIV)
               && IR_IS_BINARY(IR_DIV) && IR_IS_BINARY(IR_SHL),
               "the binary ops' blocks: ADD ... XOR an aligned block of 8,"
               " in one of 16 with DIV MOD and SHL SHR, each an aligned pair");
#define IR_IS_COMPARE(op)    IN_BLOCK(op, IR_LT, 16)  // both signs
#define IR_IS_S_COMPARE(op)  IN_BLOCK(op, IR_LT, 8)   // the signed
#define IR_IS_RELATIONAL(op) IN_BLOCK(op, IR_LT, 4)   // signed < > <= >=
#define IR_IS_U_COMPARE(op)  IN_BLOCK(op, IR_ULT, 4)
#define IR_IS_EQNE(op)       IN_BLOCK(op, IR_EQ, 2)
_Static_assert((IR_LT & 15) == 0 && IR_IS_RELATIONAL(IR_GE)
               && IR_IS_RELATIONAL(IR_GT) && IR_IS_RELATIONAL(IR_LE)
               && IR_IS_S_COMPARE(IR_EQ) && IR_IS_EQNE(IR_NE)
               && !IR_IS_RELATIONAL(IR_EQ) && (IR_EQ & 1) == 0
               && (IR_ULT & 3) == 0 && IR_IS_U_COMPARE(IR_ULE)
               && !IR_IS_S_COMPARE(IR_ULT) && IR_IS_COMPARE(IR_ULE),
               "the comparisons' blocks: the 4 relationals an aligned block,"
               " in one of 8 with EQ NE (a pair), in one of 16 with the"
               " unsigned (a block of 4)");
// The opposite comparison, the one ! gives (De Morgan): EQ <-> NE,
// LT <-> GE, GT <-> LE, and the unsigned: bit 0, since each pair is an
// even number and the next. Valid only on an IR_IS_COMPARE op.
#define IR_COMPARE_FLIP(op)  ((op) ^ 1)
_Static_assert(IR_COMPARE_FLIP(IR_EQ) == IR_NE
               && IR_COMPARE_FLIP(IR_LT) == IR_GE
               && IR_COMPARE_FLIP(IR_GT) == IR_LE
               && IR_COMPARE_FLIP(IR_ULT) == IR_UGE
               && IR_COMPARE_FLIP(IR_UGT) == IR_ULE,
               "IR_COMPARE_FLIP: a De Morgan pair isn't an even number"
               " and the next");
// The relational comparison that means the same with its operands
// exchanged (LT <-> GT, GE <-> LE): bit 1, since the 4 are an aligned
// block of 4 in that order. Valid only on an IR_IS_RELATIONAL op. (Not
// IR_COMPARE_FLIP: that is the opposite comparison.)
#define IR_COMPARE_MIRROR(op) ((op) ^ 2)
_Static_assert(IR_COMPARE_MIRROR(IR_LT) == IR_GT
               && IR_COMPARE_MIRROR(IR_GT) == IR_LT
               && IR_COMPARE_MIRROR(IR_GE) == IR_LE
               && IR_COMPARE_MIRROR(IR_LE) == IR_GE,
               "IR_COMPARE_MIRROR doesn't exchange LT and GT, GE and LE");
// An unsigned comparison, from the signed one of the same sense (LT
// to ULT, and so on); valid only on IR_IS_RELATIONAL.
#define IR_U_COMPARE(op)     ((op) + (IR_ULT - IR_LT))
_Static_assert(IR_U_COMPARE(IR_LT) == IR_ULT && IR_U_COMPARE(IR_GE) == IR_UGE
               && IR_U_COMPARE(IR_GT) == IR_UGT
               && IR_U_COMPARE(IR_LE) == IR_ULE,
               "IR_U_COMPARE: the unsigned aren't in the signed's order");
// The commutative binary ops and comparisons: x op y is y op x.
#define IR_IS_COMMUTATIVE(op) ((IR_IS_SWAPPABLE(op) && (op) != IR_SUB) \
                               || IR_IS_EQNE(op))
// The ops whose operands ccgen can exchange at no cost: the
// commutative, SUB (as SUB then NEG), and the signed relationals (as
// their mirror).
#define IR_IS_REVERSIBLE(op) (IR_IS_SWAPPABLE(op) || IR_IS_RELATIONAL(op) \
                              || IR_IS_EQNE(op))
_Static_assert(IR_IS_COMMUTATIVE(IR_ADD) && IR_IS_COMMUTATIVE(IR_XOR)
               && IR_IS_COMMUTATIVE(IR_EQ) && IR_IS_COMMUTATIVE(IR_NE)
               && !IR_IS_COMMUTATIVE(IR_SUB) && !IR_IS_COMMUTATIVE(IR_LT)
               && IR_IS_REVERSIBLE(IR_SUB) && IR_IS_REVERSIBLE(IR_LE)
               && !IR_IS_REVERSIBLE(IR_DIV) && !IR_IS_REVERSIBLE(IR_ULT),
               "IR_IS_COMMUTATIVE or IR_IS_REVERSIBLE lost a member, or"
               " gained one");
#define IR_IS_U_ARITH(op)    IN_BLOCK(op, IR_UDIV, 4)
// Any shift, signed or unsigned.
#define IR_IS_SHIFT(op)      (IR_IS_S_SHIFT(op) || (op) == IR_USHR)
_Static_assert((IR_UDIV & 3) == 0 && IR_IS_U_ARITH(IR_USHR)
               && IR_IS_SHIFT(IR_USHR) && !IR_IS_SHIFT(IR_UDIV),
               "IR_IS_U_ARITH isn't an aligned block of 4");
#define IR_NAMES_FUNC(op)    IN_BLOCK(op, IR_FUNC, 4)  // f a function
_Static_assert((IR_FUNC & 3) == 0 && IR_NAMES_FUNC(IR_CALL)
               && IR_NAMES_FUNC(IR_FUNCADDR) && !IR_NAMES_FUNC(IR_ICALL)
               && !IR_NAMES_FUNC(IR_ENDFUNC),
               "IR_NAMES_FUNC isn't FUNC, CALL and FUNCADDR, an aligned block"
               " of 4");
#define IR_IS_BRANCH(op)     IN_BLOCK(op, IR_JUMP, 4)
#define IR_REFS_LABEL(op)    IN_BLOCK(op, IR_JUMP, 8)
_Static_assert((IR_JUMP & 7) == 0 && IR_IS_BRANCH(IR_BRFALSE)
               && IR_IS_BRANCH(IR_BRTRUE) && !IR_IS_BRANCH(IR_SEL)
               && IR_REFS_LABEL(IR_SEL) && IR_REFS_LABEL(IR_CASETO)
               && !IR_REFS_LABEL(IR_JOIN) && !IR_REFS_LABEL(IR_LABEL),
               "IR_IS_BRANCH isn't JUMP BRFALSE BRTRUE, an aligned block of"
               " 4, in one of 8 with SEL and CASETO (IR_REFS_LABEL)");
// BRTRUE if t, else BRFALSE; t must be 0 or 1 (BRTRUE is BRFALSE | 1).
#define IR_BR_IF(t)          (IR_BRFALSE | (t))
_Static_assert(IR_BR_IF(0) == IR_BRFALSE && IR_BR_IF(1) == IR_BRTRUE,
               "IR_BR_IF: BRFALSE isn't even with BRTRUE the next");
#define IR_IS_LOOP_MARKER(op) IN_BLOCK(op, IR_LOOP, 4)
_Static_assert((IR_LOOP & 3) == 0 && IR_LOOPEND - IR_LOOP == 3,
               "IR_IS_LOOP_MARKER isn't the 4 loop markers, an aligned"
               " block");
// The access ops' bits (IR_LOCAL ... IR_STOREB, and their volatile
// forms), valid only on them: IR_IS_ACCESS. The char form of a word
// access, if c (which must be 0 or 1, as IR_ACC_BYTE is 1); a load's
// store, which keeps its value for LOCAL and GLOBAL (SETLOCAL), not
// for LOAD (STORE); the volatile form, and the plain form of either.
// IR_FORM adds form, a mix of IR_ACC_BYTE and IR_VOLATILE, to an
// access op. (Q_VOLATILE, in type.h, is IR_VOLATILE << 4: parse.c's
// acc_form() relies on it.)
#define IR_ACC_BYTE          0x01
#define IR_ACC_STORE         0x02
#define IR_VOLATILE          0x20
#define IR_IS_ACCESS(op)     (((op) & ~IR_VOLATILE) < IR_LOCALADDR)
#define IR_IS_VOLATILE(op)   ((op) >= (IR_LOCAL | IR_VOLATILE) \
                              && (op) <= (IR_STOREB | IR_VOLATILE))
#define IR_CHAR_FORM(op, c)  ((op) | (c))
#define IR_WORD_FORM(op)     ((op) & ~IR_ACC_BYTE)
#define IR_STORE_FORM(op)    ((op) | IR_ACC_STORE)
#define IR_PLAIN_FORM(op)    ((op) & ~IR_VOLATILE)
#define IR_FORM(op, form)    ((op) | (form))
_Static_assert(IR_LOCAL == 0 && IR_IS_ACCESS(IR_STOREB)
               && !IR_IS_ACCESS(IR_LOCALADDR) && IR_LOCALADDR == IR_STOREB + 1,
               "IR_IS_ACCESS isn't the 12 ops from 0 up to IR_LOCALADDR");
_Static_assert(IR_CHAR_FORM(IR_LOCAL, 1) == IR_LOCALB
               && IR_CHAR_FORM(IR_SETLOCAL, 1) == IR_SETLOCALB
               && IR_CHAR_FORM(IR_GLOBAL, 1) == IR_GLOBALB
               && IR_CHAR_FORM(IR_SETGLOBAL, 1) == IR_SETGLOBALB
               && IR_CHAR_FORM(IR_LOAD, 1) == IR_LOADB
               && IR_CHAR_FORM(IR_STORE, 1) == IR_STOREB
               && IR_ACC_BYTE == 1,
               "IR_CHAR_FORM: a char access isn't its word access | 1");
_Static_assert(IR_WORD_FORM(IR_LOADB) == IR_LOAD
               && IR_WORD_FORM(IR_LOAD) == IR_LOAD,
               "IR_WORD_FORM isn't IR_CHAR_FORM undone");
_Static_assert(IR_STORE_FORM(IR_LOCAL) == IR_SETLOCAL
               && IR_STORE_FORM(IR_LOCALB) == IR_SETLOCALB
               && IR_STORE_FORM(IR_GLOBAL) == IR_SETGLOBAL
               && IR_STORE_FORM(IR_GLOBALB) == IR_SETGLOBALB
               && IR_STORE_FORM(IR_LOAD) == IR_STORE
               && IR_STORE_FORM(IR_LOADB) == IR_STOREB,
               "IR_STORE_FORM: a store isn't its load | IR_ACC_STORE");
// The volatile forms take numbers no other op has: IR_VOLATILE isn't
// one of an access op's bits, and the forms fall where no op is (the
// ops after the access ops stop below them, and the next are past).
_Static_assert(((IR_ACC_BYTE | IR_ACC_STORE | IR_STOREB) & IR_VOLATILE) == 0
               && IR_IS_VOLATILE(IR_LOCAL | IR_VOLATILE)
               && IR_IS_VOLATILE(IR_STOREB | IR_VOLATILE)
               && IR_PLAIN_FORM(IR_STOREB | IR_VOLATILE) == IR_STOREB
               && IR_NOT < (IR_LOCAL | IR_VOLATILE)
               && (IR_STOREB | IR_VOLATILE) < IR_UDIV,
               "IR_VOLATILE overlaps an access op's bits, or its forms"
               " overlap other ops");
// The op-indexed tables (irin.c's stk_effect[], lower.c's lw_kind[],
// lw_op[] and ir_names[]) have IR_NOPS entries, so it must be past
// every op: a spot check of each block's last.
_Static_assert((IR_STOREB | IR_VOLATILE) < IR_NOPS && IR_SHR < IR_NOPS
               && IR_ULE < IR_NOPS && IR_CASEVAL < IR_NOPS
               && IR_LOOPEND < IR_NOPS,
               "an IR op is past IR_NOPS, so it has no entry in the op-indexed"
               " tables");

// The bit that marks an IR op followed by a one-byte operand. An IR
// op's tag is its number, below IR_TAG_END ($60), so up to 96 ops; a
// record's tag is its letter (obj.fmt.h) in lowercase, IR_REC(), $61
// to $7A, so that the ops can have the numbers the uppercase letters
// would take. ccgen copies a data record to the stream file with its
// uppercase letter.
#define IR_BYTE_OP  0x80
#define IR_TAG_END  0x60
#define IR_REC_CASE 0x20
#define IR_REC(rec) ((rec) | IR_REC_CASE)
// An op's tag, with or without IR_BYTE_OP, is a byte, and no record's
// (an uppercase letter's lowercase) is one.
_Static_assert(IR_NOPS <= IR_TAG_END && IR_TAG_END <= IR_REC('A')
               && IR_REC('Z') < IR_BYTE_OP && IR_BYTE_OP + IR_TAG_END <= 0x100,
               "an IR op's tag is also a record's, or isn't a byte");

// A name record's flags byte: NAME_RETURNS set if the function returns
// a value.
#define NAME_RETURNS 0x80

#endif
