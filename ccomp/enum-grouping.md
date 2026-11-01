# Grouping the enums for set membership (proposal)

The task is in `src/,enum-set-membership.md`. This is the proposed design:
which sets exist, how the enums in `src/*.h` can be ordered so the sets are
ranges, and what #defines to add to the .h files so that the .c files test
"tok is a kind of X" with a range check instead of a chain of comparisons.
The goals, in order:

1. **Concentrate the ordering knowledge in the .h files.** A .c file says
   "is this token a type starter?" and does not need to know which tokens
   those are or how they are numbered. Adding a member to a set means
   placing the new member in the enum "at the right place" and adjusting
   the #define, without touching most of the .c code.
2. **Reduce generated size, mostly in cccomp**, whose module file
   (42,743 bytes, small build, 2026-10-31) is the one that does not fit.
   A chain of N equality checks compiles to N loads-and-compares plus
   N-1 ORs; a range check compiles to two compares and an AND, whatever
   the set's size. parse.c's type-name checks are the big ones.

This is a proposal, for discussion before coding.

## 1. The sets, found by the task's grep

`grep -E '[A-Z][A-Z].*\|\|.*[A-Z][A-Z].*\|\|.*[A-Z][A-Z]' src/*.c` finds
the chains of three or more terms. Two-term chains and the places that
already use range checks are included too, since they use the same sets.
Which program a file belongs to is from the Makefile (cccomp = cccomp.c
nam.c tokin.c name.c type.c parse.c irout.c; ccgen = irin.c lower.c
peep.c emit.c helper.c; ccpp = ppname.c pplex.c ppmac.c ppif.c ppout.c
pp.c; ccrel = link.c; diag.c and bio.c are common).

| Set of members | Used in | Enum's home | Program |
|---|---|---|---|
| {T_INT T_CHAR T_VOID T_CONST T_VOLATILE T_STRUCT T_ENUM T_SIGNED T_UNSIGNED} | parse.c:473 is_type_start | tokin.h | cccomp |
| {T_INT T_CHAR T_SIGNED T_UNSIGNED T_CONST T_VOLATILE} | parse.c:526 parse_int_type's loop | tokin.h | cccomp |
| {T_INT T_CHAR T_SIGNED T_UNSIGNED} | parse.c:560 parse_base_type | tokin.h | cccomp |
| {T_CONST T_VOLATILE} | parse.c:504, 528 parse_quals, parse_int_type | tokin.h | cccomp |
| {T_INT T_CHAR} | parse.c:532, 535 | tokin.h | cccomp |
| {T_STRUCT T_ENUM} | parse.c:555 | tokin.h | cccomp |
| {T_STATIC T_EXTERN} | parse.c:4384 | tokin.h | cccomp |
| {T_CASE T_DEFAULT} | parse.c:3621 | tokin.h | cccomp |
| {T_ANDAND T_OROR} | parse.c:2918 | tokin.h | cccomp |
| {IR_LT IR_GT IR_LE IR_GE} | parse.c:2467 is_relational | ir.h | cccomp |
| {IR_EQ .. IR_GE} | parse.c:801 is_compare (already a range) | ir.h | cccomp |
| {IR_ADD .. IR_SHR} | parse.c:2477 op_name (already a range) | ir.h | cccomp |
| {IR_DIV IR_MOD} | parse.c:725, 740 | ir.h | cccomp |
| {IR_SHL IR_SHR} | parse.c:727, 741, 2525, 2985 | ir.h | cccomp |
| {IR_DIV IR_MOD IR_SHR} | parse.c:2533 unsigned_op | ir.h | cccomp |
| {IR_SHL IR_SHR IR_USHR} | parse.c:2578 binary | ir.h | cccomp |
| {IR_ADD IR_SUB} | parse.c:2929 | ir.h | cccomp |
| {K_VAR K_DEREF K_ELEM} | parse.c:1085 is_place | parse.c (local) | cccomp |
| {IR_JUMP IR_BRFALSE IR_BRTRUE} | irin.c:333 block_ok | ir.h | ccgen |
| {IR_UDIV IR_UMOD IR_USHR} | irin.c:522 hold | ir.h | ccgen |
| {IR_ULT .. IR_UGE} | irin.c:413 (already a range) | ir.h | ccgen |
| {IR_JUMP IR_BRFALSE IR_BRTRUE IR_SEL IR_CASETO} | irin.c:1260 do_op | ir.h | ccgen |
| {IR_ADD IR_SUB} | irin.c:869 | ir.h | ccgen |
| {IR_LT IR_LE} / {IR_LE IR_GE} | irin.c:938, 1066 (two-term) | ir.h | ccgen |
| {REC_DATA REC_DREL REC_STR REC_ZERO} | irin.c:1390 | obj.h | ccgen |
| {OP_LEAVE OP_RET OP_BRNCH OP_SEL} | peep.c:104 | mop.h (VM-fixed) | ccgen |
| {OP_BRFLS OP_BRTRU} | peep.c:110 is_test_branch | mop.h | ccgen |
| {OP_INCR OP_DECR OP_NEG OP_COMP OP_ADDI OP_SUBI} | peep.c:187 is_unary | mop.h | ccgen |
| {OP_SLW OP_SLB OP_SAW OP_SAB} | peep.c:249 store_len | mop.h | ccgen |
| {OP_SW OP_SB} | peep.c:251 | mop.h | ccgen |
| {OP_ADD OP_SUB} | peep.c:265, 332, 367 | mop.h | ccgen |
| {OP_CW OP_CB OP_CFFB OP_LA} | peep.c:319 | mop.h | ccgen |
| {OP_NEG OP_COMP} | peep.c:357 | mop.h | ccgen |
| {OP_ISEQ OP_ISNE} | peep.c:414 | mop.h | ccgen |
| {OP_BREQ OP_BRNE} | peep.c:424 | mop.h | ccgen |
| {OP_LLW OP_LLB OP_LLA OP_LA OP_DUP} | peep.c:496 frame_push | mop.h | ccgen |
| {OP_LLW OP_LLB OP_ADDLB OP_ADDLW OP_IDXLB OP_IDXLW} | peep.c:505 reads_local | mop.h | ccgen |
| {FIX_DSYM FIX_HEAP} | emit.c:387 | mop.h | ccgen |
| {TC_ID TC_NUM TC_UNUM TC_KEYWORD} | ppmac.c:156, 173, 193; ppout.c:180 | token.h | ccpp |
| {TC_PARAM TC_PARAM_RAW TC_HASH_PARAM} | ppmac.c:160, 625 | pplex.h | ccpp |
| {F_WORD F_REL F_ENTER} | link.c:743 | link.c (local) | ccrel |

By program, cccomp's sets are the token sets (tokin.h) and the IR sets
(ir.h, used by parse.c); ccpp's are the TC_* sets; ccgen's are the OP_*,
REC_* and FIX_* sets plus some IR sets. The proposals below are in that
priority order.

## 2. tokin.h: reorder the keywords, add T_IS_* macros

**Priority 1. This is where cccomp's savings are.**

The keyword values T_INT ... T_UNSIGNED have to stay in the order of the
token stream's codes (CCPP.md, "$80 up, in this order"): tokin.c maps a
stream code c to `T_INT + (c - TC_KEYWORDS)`, pplex.c writes
`TC_KEYWORDS + i` for the i-th string of its keywords[] table, and the
spellings for messages 262-285 of ccish.msg are in T_INT's order. So
reordering the keywords is a token-stream format change: TOK_VERSION goes
1 to 2, and five places change together (below). The NAME.i files are
intermediate (ccpp writes them and cccomp reads them in the same run), so
a version change costs nothing at run time: an old NAME.i is simply
reported as the wrong version (tokin.c, fatal 230).

The current order, with each keyword's index in the stream:

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| int | void | return | if | else | while | do | for |
| 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
| break | continue | char | sizeof | static | const | volatile | struct |
| 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 |
| switch | case | default | typedef | enum | extern | signed | unsigned |

The type-name tokens are scattered: int and void at 0, 1; char at 10;
const and volatile at 13, 14; struct at 15; enum at 20; signed and
unsigned at 22, 23.

**Proposed order.** The type-name tokens first, in the groups the parser
actually tests; then the rest, roughly as now:

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| int | char | signed | unsigned | void | const | volatile | struct |
| **8** | **9** | **10** | **11** | **12** | **13** | **14** | **15** |
| enum | return | if | else | while | do | for | break |
| **16** | **17** | **18** | **19** | **20** | **21** | **22** | **23** |
| continue | sizeof | static | extern | switch | case | default | typedef |

Every set the parser tests becomes a range, and the groups are visible
in the table:

- 0-3: int, char, signed, unsigned — the int-type specifiers;
- 0-4: those plus void;
- 5-6: the qualifiers;
- 7-8: the tagged types;
- 0-8: everything that can start a type name (is_type_start's 9 tokens);
- 18-19: the storage-class keywords;
- 20-22: switch, case, default (21-22: the case labels).

**The enum, as it would read** (tokin.h). Only the keyword block moves;
T_EOF, T_ID, T_NUM, T_STR, T_KEYWORD and the operators T_EQ ...
T_ELLIPSIS keep their positions, so tokin.c's `tok >= T_INT` and
`tok >= T_EQ` tests in tok_desc(), and its arithmetic on the message
numbers, still hold:

```c
enum {
  T_EOF = 0,
  T_ID = 256, T_NUM, T_STR,
  /* the type-name tokens, grouped so that T_IS_* below is a range:
   * the int-type specifiers, then void, the qualifiers, the tagged
   * types */
  T_INT, T_CHAR, T_SIGNED, T_UNSIGNED,
  T_VOID,
  T_CONST, T_VOLATILE,
  T_STRUCT, T_ENUM,
  /* the other keywords: the control-flow ones, then the rest,
   * with the storage-class keywords and the switch keywords each
   * kept adjacent */
  T_RETURN, T_IF, T_ELSE, T_WHILE, T_DO, T_FOR, T_BREAK, T_CONTINUE,
  T_SIZEOF,
  T_STATIC, T_EXTERN,
  T_SWITCH, T_CASE, T_DEFAULT,
  T_TYPEDEF,
  /* any other C keyword: reported as "not supported yet" */
  T_KEYWORD,
  /* multi-character operators (not all are supported by the parser yet) */
  T_EQ, T_NE, T_LE, T_GE, T_SHL, T_SHR, T_ANDAND, T_OROR, T_INC, T_DEC,
  T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ, T_MODEQ, T_ANDEQ, T_OREQ, T_XOREQ,
  T_SHLEQ, T_SHREQ, T_ARROW, T_ELLIPSIS
};

/* The sets of tokens the parser tests, each a range in the order
 * above. A new token joins a group by taking a place in it. */
#define T_IS_TYPE_START(t)  ((t) >= T_INT && (t) <= T_ENUM)
#define T_IS_INT_TYPE(t)    ((t) >= T_INT && (t) <= T_UNSIGNED)
#define T_IS_QUAL(t)        ((t) >= T_CONST && (t) <= T_VOLATILE)
#define T_IS_TAGGED(t)      ((t) >= T_STRUCT && (t) <= T_ENUM)
#define T_IS_STORAGE(t)     ((t) >= T_STATIC && (t) <= T_EXTERN)
#define T_IS_CASE_LABEL(t)  ((t) >= T_CASE && (t) <= T_DEFAULT)
```

**How the .c code changes** (parse.c; the task's example, made precise):

```c
static int is_type_start(void)
{
  return T_IS_TYPE_START(tok)
         || (tok == T_ID && typedef_type(tok_val) >= 0);
}
```

```c
static int parse_quals(void)
{
  int q = 0;
  while (T_IS_QUAL(tok)) {
    q |= tok == T_CONST ? Q_CONST : Q_VOLATILE;
    next();
  }
  return q;
}
```

```c
static int parse_int_type(int *q)
{
  int base = 0, sign = 0;
  while (T_IS_INT_TYPE(tok) || T_IS_QUAL(tok)) {
    if (T_IS_QUAL(tok)) {
      *q |= parse_quals();
      continue;
    }
    if (tok == T_INT || tok == T_CHAR ? base != 0 : sign != 0)
      error(226 /* "conflicting or repeated type specifier:" */,
            tok_desc());
    if (tok == T_INT || tok == T_CHAR)
      base = tok;
    else
      sign = tok;
    next();
  }
  ...  /* unchanged from here */
}
```

(The int/char versus signed/unsigned distinction stays written out: those
four names are the anchors, and a two-term check is no bigger than a
range.) parse_base_type() likewise: `T_IS_TAGGED(tok)` for the tag test,
`T_IS_INT_TYPE(tok)` for the int-type branch. parse_declaration():
`T_IS_STORAGE(tok)`, and the error's `storage == T_STATIC ?` stays.
parse_case_labels(): `T_IS_CASE_LABEL(tok)`.

**Size.** Rough, to be confirmed by tools/measure.py. Each term of
`tok == T_X` lowers to a load of tok (LA, 3 bytes), a constant (CW, 3
bytes: the keywords are 259 up, past CB's 255) and ISEQ then NEG
(PLASMA's compare gives $FFFF, the NEG makes C's 0 or 1), plus an IOR
into the running result — roughly 9 bytes per term after the first. A
range check is two such compares (no IORs, a short-circuit branch
between them instead). So is_type_start()'s 9 terms (roughly 70 bytes
of compares) become two compares (roughly 18); parse_int_type()'s 6-term
loop (roughly 50) becomes two ranges and an OR (roughly 30);
parse_base_type()'s 4-term branch (roughly 35) becomes one range
(roughly 18). The two-term checks (qualifiers, tagged, storage, case
labels) come out about even, and are taken for the name, not the size.
Both big functions are in cccomp, and both run on every type name the
front end parses, so the change shrinks cccomp's code and speeds its
type parsing. Counting the sites, the saving is on the order of 100
bytes of cccomp code — a fraction of the ~2,750 bytes cccomp must still
lose — and the rest of the benefit is the organization.

**What changes with the reorder** (five files, one commit, all
mechanical):

- tokin.h: the enum block and the T_IS_* macros.
- token.h: `#define TC_STRUCT (TC_KEYWORDS + 15)` becomes `+ 7`
  (struct's new index). Note for later: this is exactly the hidden
  coupling the grouping removes — the offset lives in the table, next
  to the order.
- pplex.c: the 24 strings of keywords[] move to the new order.
- ccish.msg: the texts of messages 262-285 move to the new order.
  Their numbers are unchanged, so error() calls and the error tests
  are untouched; tests/check_msgs.sh still passes.
- CCPP.md: the keyword table ($80 up) and the sentence that names
  tokin.h and pplex.c as the other copies of the order.
- token.h: TOK_VERSION 1 to 2.
- tokin.c, ppout.c, ppif.c: no change; they are already
  order-independent (arithmetic on the code).

**Risks.** The main one is doing the edits in lockstep, in one commit;
if one lags, ccpp writes codes cccomp maps wrong, or a message names the
wrong keyword. The tests do not catch a wrong-but-consistent mapping
directly, but tok_desc() prints keyword spellings in many error
messages, and the error tests (tests/errors/) compare the message text,
so a misplaced string shows up there.

## 3. ir.h: add IR_IS_* macros; move JOIN after SEL

The IR ops are already grouped by their designers — the comments in
ir.h are the documentation of it — and every set in section 1 is either
already a range or one compare off of one. The work is to lift the
range checks that live inline in the .c files up into ir.h, to
document the ordering constraints that are now implicit, and to move
IR_JOIN one slot (section 3.1).

### 3.1 Reorder JOIN and SEL (Q4)

IR_SEL (46) names a case table in its a operand; IR_JOIN (45) does not,
and the label-naming ops JUMP (42), BRFALSE (43 and BRTRUE (44) are the
only other ops with a in a label. Moving SEL to 45 and JOIN to 46 makes
the ops that reference a label in a exactly the range JUMP through SEL
(CASETO's a is a case-table label too, but it sits with the case ops at
47 through 49; the range plus CASETO is written once, below).

That is a file format change: IR_VERSION goes 6 to 7, irin.c's
stk_effect[] and lower.c's ir_names[], lw_kind[] and lw_op[] each move
the SEL/JOIN entries one slot (the table stays one entry per op, in op
number order; no other entry moves), and ir.h's comment at the control
flow ops says JUMP through SEL name a label in a. cccomp's IR writer
(irout.c) writes op numbers as tags, so it needs no change; nothing
else in the tree keys on the two numbers.

**The macros, in ir.h** (after the op enum, beside IR_VERSION):

```c
/* The sets of ops the compiler tests in more than one place, each a
 * range in the order above (approach.md, decision 26); a set tested
 * in only one place stays a plain comparison there. Changing the
 * order changes IR_VERSION; the constraints on it are in the comments
 * at the ops. */
#define IR_IS_BINARY(op)     ((op) >= IR_ADD && (op) <= IR_SHR)
#define IR_IS_COMPARE(op)    ((op) >= IR_EQ && (op) <= IR_GE)
#define IR_IS_RELATIONAL(op) ((op) >= IR_LT && (op) <= IR_GE)
#define IR_IS_DIVMOD(op)     ((op) >= IR_DIV && (op) <= IR_MOD)
#define IR_IS_S_SHIFT(op)    ((op) >= IR_SHL && (op) <= IR_SHR)
#define IR_IS_U_COMPARE(op)  ((op) >= IR_ULT && (op) <= IR_UGE)
#define IR_IS_U_ARITH(op)    ((op) >= IR_UDIV && (op) <= IR_USHR)
#define IR_IS_BRANCH(op)     ((op) >= IR_JUMP && (op) <= IR_BRTRUE)
#define IR_REFS_LABEL(op)    (((op) >= IR_JUMP && (op) <= IR_SEL) \
                              || (op) == IR_CASETO)
```

A set tested in only one place is left a plain comparison there:
the {DIV, MOD, SHR} set unsigned_op() turns into its U_ form
(parse.c:2533) and the {SHL, SHR, USHR} set in binary() (parse.c:2578).
A note on cost, measured in Step 2: on this VM a bound test costs one
byte more than an equality test (a bound is a load, a compare and a
branch; an equality is a load and a branch), so a range over two
consecutive ops costs two bytes more than the two equalities it
replaces. The two-element sets (IR_IS_DIVMOD, IR_IS_S_SHIFT) are kept
anyway because they are tested in two and four places and name the C
meaning; the single-use sets were not worth the bytes. Reordering the
binary group to make {DIV, MOD, SHR} a pure range (DIV MOD SHR SHL)
would save two ops at one call site in cccomp but would change the
file format and realign four parallel tables for that. Not proposed.

**Where they are used:**

- parse.c:801 is_compare() and parse.c:2467 is_relational(): the
  function bodies become the macros (or the functions go and the
  macros are used at the eight call sites — either way the range is
  written once, in ir.h).
- parse.c:2477 op_name(): `IR_IS_BINARY(op)`, with names[] still
  indexed by `op - IR_ADD`.
- parse.c:725, 727, 740, 741 (check_right_const, fold):
  IR_IS_DIVMOD, IR_IS_S_SHIFT.
- parse.c:2525, 2985 (unsigned_op's shift test, the constant-holding
  path): IR_IS_S_SHIFT. parse.c:2533 (the U_ form test) and
  parse.c:2578 (binary's shift result) stay plain comparisons.
- irin.c:333 (block_ok): IR_IS_BRANCH.
- irin.c:413 (is_unsigned_cmp): IR_IS_U_COMPARE.
- irin.c:522 (hold): IR_IS_U_ARITH.
- irin.c:1260 (do_op): IR_REFS_LABEL.

**Sizes, measured (Step 2).** With tools/fsize.py on measure.py's
listings, per function in cccomp: is_relational shrank (a four-term
chain became a range); check_right_const and fold grew (each has a
two-element set, section 3's cost note); parse_expr grew a little
(its shift test is the two-element range); binary and unsigned_op are
back at their old sizes once their single-use sets stayed inline.
The JOIN/SEL reorder is size-neutral in cccomp but shrank ccgen's
code (its lw_kind and stk_effect entries moved one slot). The point
is the same as for the tokens: the sets and the ordering that makes
them ranges are stated in one place, and parse.c and irin.c stop
restating them.

**Ordering constraints to write in ir.h** (today implicit in comments
and in parse.c's arithmetic):

- LT, GT and LE, GE stay adjacent pairs, IR_LT even: irin.c reverses
  one by flipping bit 0. (Already commented at the ops.)
- ULT, UGT, ULE, UGE stay in the order of LT, GT, LE, GE, and the
  whole group stays at the offset IR_ULT - IR_LT from the signed
  comparisons: parse.c finds an unsigned comparison by
  `op + IR_ULT - IR_LT` (parse.c:2531, 2563, 3005).
- The loop markers must stay last (IR_NOPS counts past them;
  irin.c takes any op >= IR_LOOP into the loop code).
- Every op number is an IR file tag: below IR_TAG_END (0x40) alone, or
  with IR_BYTE_OP (0x80) for a one-byte operand — so at most 64 ops,
  of which 63 are used.

**One optional reorder, not proposed.** irin.c:1260 tests
{JUMP, BRFALSE, BRTRUE, SEL, CASETO} ("the op names a label in a").
JOIN sits between BRTRUE and SEL; swapping them would make the first
four a range (IR_JUMP .. IR_SEL) plus CASETO. That is a ccgen-only
savings of a couple of ops; it changes IR_VERSION and the parallel
tables in lower.c (ir_names[], lw_kind[], lw_op[]) and irin.c
(stk_effect[]) for nothing cccomp gains. Leave it out of this change.

## 4. token.h: TC_IS_WORDVAL; move TC_STR after TC_KEYWORD

The set worth a macro is the tokens that carry a 16-bit value in the
stream: TC_ID, TC_NUM, TC_UNUM and TC_KEYWORD. They were not
contiguous (TC_STR sat between TC_UNUM and TC_KEYWORD, at 0x07), so
TC_STR moves to 0x08, after TC_KEYWORD: the 16-bit-value tokens are
the range 0x04 through 0x07, TC_TAG stays at 0x09, and the
stream's version byte goes 2 to 3 (the header is CTK plus the
version; CCPP.md's table and example move with it, as do the
tests/pp token streams' prose). Every use of the codes in the .c
files is symbolic, so only token.h, the version, and the documents
change.

```c
/* The tokens that carry a 16-bit value in the stream, their bytes
 * the code plus the value's low and high bytes (CCPP.md). */
#define TC_IS_WORDVAL(c)   ((c) >= TC_ID && (c) <= TC_KEYWORD)
```

Used at the four places that write or read the token: ppmac.c's
token_len(), encode() and decode() (156, 173, 193 — token_len's
switch case labels stay as written: a case label can't be a range),
and ppout.c's out_token() (180). A 4-term chain becomes a range in
ccpp's hot path (every macro body byte and every token out). The
two-term sets {TC_NUM, TC_UNUM} (ppif.c:117) and {TC_ID, TC_KEYWORD}
(pp.c:118, 146; ppif.c:57, 63; ppmac.c:243, 391, 677) are left as
they are: a two-term check is the same size as a range, and each
pair is tested only in one or a few places.

TC_STRUCT (section 2) moves with the keyword reorder.

## 5. mop.h: the VM's numbering, documented; a few OP_IS_* macros

The OP_* values are PLASMA's opcodes (../../doc/Bytecodes.md); they
cannot be moved, and the pseudo-ops (0x100 up) are markers, not a set.
What can be done is to write down the families the VM's own numbering
makes — the code already exploits several of them, silently — and to
give names to the sets peep.c tests.

**Families, for a comment at the enum** (the ranges the code uses or
could use):

- constant pushes: CN 0-15 ($00-$1E), MINUS1, CB, CW, CFFB
  (mop_const_value() in lower.c recognizes exactly these);
- zero tests, no value popped: BREQ, BRNE ($22, $24);
- comparisons of two values: ISEQ .. ISLE ($40-$4A);
- test branches, one value popped: BRFLS, BRTRU ($4C, $4E);
- branches and calls: BRNCH, SEL, CALL, ENTER, LEAVE, RET ($50-$5C);
- loads, all eight: LB LW (through the address on the stack), LLB LLW
  (the frame), LAB LAW (a global), DLB DLW (a global, keeping the
  value), $60-$6E in that order;
- stores, all eight, the same way: SB SW, SLB SLW, SAB SAW, DAB DAW,
  $70-$7E;
- arithmetic on the eval stack, no memory: ADD .. IDXW, $82-$9E
  (peep.c's frame_expr() already tests this range);
- the counted-loop branches: $A0-$AA;
- the frame-address adds: ADDLB ADDLW ADDAB ADDAW, then the same with
  twice the value, IDXLB .. IDXAW, $B0-$BE: each group of four is in
  the loads' order (LL, L, A, A forms), and local is the lower two of
  the four (peep.c's fused_op() and emit.c's `op & 4` rely on it).

**The macros, for the sets peep.c tests:**

```c
/* A test branch: pops a value and branches on 0 or not 0. */
#define OP_IS_TEST_BRANCH(op)  ((op) >= OP_BRFLS && (op) <= OP_BRTRU)
/* An absolute store that keeps no value: SLB SLW SAW SAB. */
#define OP_IS_PLAIN_STORE(op)  ((op) >= OP_SLB && (op) <= OP_SAW)
/* A store through the address on the stack: SB SW. */
#define OP_IS_ADDR_STORE(op)   ((op) >= OP_SB && (op) <= OP_SW)
/* A read of the frame: a local load, or an add of one (peep.c's
 * reads_local). The VM numbers the adds in the loads' order. */
#define OP_IS_LOCAL_READ(op)   ((op) >= OP_LLB && (op) <= OP_LLW
                                || (op) >= OP_ADDLB && (op) <= OP_ADDLW
                                || (op) >= OP_IDXLB && (op) <= OP_IDXLW)
```

is_test_branch() (peep.c:110), store_len() (249, 251) and
reads_local() (505) use them. The other peep.c chains are two-term on
already-adjacent pairs (ADD/SUB, NEG/COMP, ISEQ/ISNE, BREQ/BRNE) or
switches with one case per op (invert_branch, plain_store,
keeping_store, absolute_op): a range buys nothing there, and the
switches stay. is_unconditional_exit() ({LEAVE, RET, BRNCH, SEL},
$5A, $5C, $50, $52) and the {CW, CB, CFFB, LA} set (319) are not
ranges in the VM's numbering; they keep their explicit forms.

**FIX_*.** FIX_DSYM and FIX_HEAP are the two fixups ccrel may rewrite
(emit.c:387). They are not adjacent (FIX_HELPER is between them). The
values are internal to ccgen — no file carries them — so they could be
reordered at no format cost. It would not change the size of the
two-term check. Proposed: leave the order, add a comment that DSYM and
HEAP are the pair ccrel may rewrite.

**Bonus, found while looking.** frame_expr() in peep.c (513) tests
`win[i].op != OP_INCR && win[i].op != OP_DECR` after
`!(win[i].op >= OP_ADD && win[i].op <= OP_IDXW)`. INCR ($8C) and DECR
($8E) are inside that range, so the two comparisons can never be
reached true: they are dead. Removing them is a pure saving (AGENTS.md
welcomes removing operations that have no effect).

## 6. obj.h: REC_IS_PASSTHROUGH

The record tags are letters in the file, chosen so a dump reads
(obj.h says so). They are not reordered: making D, R, S and Z adjacent
would cost the readable letters and bump OBJ_VERSION to save nothing
(four compares either way). The set gets a name in obj.h:

```c
/* A record the back end copies to the stream as it is: data, a
 * relocation, a string or zero-filled bytes. */
#define REC_IS_PASSTHROUGH(t)  ((t) == REC_DATA || (t) == REC_DREL
                                || (t) == REC_STR || (t) == REC_ZERO)
```

Used at irin.c:1390. (link.c already expresses its record sets a
different way, as the small char tables data_tags[], str_tags[],
rld_tags[] and helper_tags[] — that is the right tool there, since the
sets differ per scan.)

REL_* (1-5) is contiguous but no code tests it as a set; SYM_* is bit
flags, tested by bits; HELP_* is indexed 0..NHELPERS-1 through bit
masks. No change.

## 7. pplex.h: TC_IS_PARAM

TC_PARAM, TC_PARAM_RAW and TC_HASH_PARAM are 0x10, 0x11, 0x12 —
adjacent, and the set ppmac.c tests ("a macro parameter token, in one
of its three forms"):

```c
/* A macro parameter: the parameter itself, the raw form (an operand
 * of ##), or the # form. Adjacent. */
#define TC_IS_PARAM(c)  ((c) >= TC_PARAM && (c) <= TC_HASH_PARAM)
```

Used at ppmac.c:625 (call()'s "which slot does this token name") and,
if wanted, at token_len() (160). PM_TEXT, PM_DIRECTIVE, PM_SKIP are
states compared one at a time; no set.

## 8. Headers checked, no change needed

- **type.h.** TK_* and TY_* are compared one at a time or switched on;
  no set-membership chains anywhere (parse.c's type tests are on the
  TY_ values and the Q_ bits, all single). No change. Constraint noted
  for the future: type.c's `tkind[] = {TK_VOID, TK_CHAR, TK_INT,
  TK_UINT}` ties each basic type's TK_ value to its TY_ index, so
  reordering TK_* would move that initializer too.
- **name.h** (ID_CONST, ID_TYPEDEF), **parse.h** (the FN_* bits), the
  V_* and VL_* bits and the PREC_ levels in parse.c: single values or
  bit flags; no sets.
- **cclimits.h, build.h, bio.h, diag.h, fixed.h, emit.h, helper.h,
  irin.h, irout.h, lower.h, link.h, nam.h, peep.h, pp.h, ppif.h,
  ppname.h, ppout.h:** no enums with sets.

## 9. Enums that live in .c files

The task's grep found two of these; they are not in .h files, but the
same treatment applies when the .h work is done:

- **K_* (parse.c:899), Operand's kinds.** K_VAR, K_DEREF, K_ELEM — the
  places — are already adjacent (3, 4, 5). is_place() becomes
  `return o->kind >= K_VAR && o->kind <= K_ELEM;`. Note: the comment
  above the enum says "the last three kinds are places", which is
  stale (the last three are K_STORE, K_JUMP, K_ADDR; the places are the
  middle three) — fix the comment at the same time.
- **F_* (link.c:616), the listing's operand formats.** F_WORD, F_REL,
  F_ENTER — the three-byte ops — are already adjacent (2, 3, 4).
  link.c:743 becomes `fmt >= F_WORD && fmt <= F_ENTER`.
- **SW_*, U_*, PRE_*, L_* (irin.c:221-223, lower.c:52):** internal
  states and kinds, each compared one at a time; no sets.

## 10. Naming

ccish builds all four passes into one program (the Makefile's ccish
target), so every macro in every header shares one namespace. Proposed
rule: a predicate macro is named after its enum's prefix — T_IS_*,
IR_IS_*, OP_IS_*, TC_*, REC_*, K_ — and takes the value as its single
argument. The prefixes keep the token sets (T_*), the stream codes
(TC_*) and the IR ops (IR_*) from colliding; they also say at the use
site which domain the test is in. (The task note's
SIMPLE_TYPE_START(tok) is the same idea with the concept first; the
prefix-first form is what the other headers already do —
FRAME_TEMP(k), TC_STRUCT.)

## 11. How the change is measured, and what to expect

Per step, in the order AGENTS.md prescribes:

1. `python3 tools/measure.py` before and after (the git-worktree
   comparison for the "before"), so cccomp's code, data and module file
   are compared, not just reported.
2. `./tests/run.sh` (or `make test`): all 381 tests pass, and the small
   build still writes byte-identical objects and modules. The macros
   test no feature macro, so the two builds evaluate them the same.
3. `python3 tools/opcount.py <worktree-base>`: the generated code of
   every run test is unchanged in op count and output. Nothing here
   touches what the compiler emits for a user program — the T_ reorder
   changes how ccpp and cccomp number keywords between them, the IR
   macros change only the compiler's own tests — so the expectation is
   zero tests changed. If any count moves, that is a bug in the change.

Expected direction: cccomp's code smaller by about 100 bytes (the
token sets in parse.c, section 2's count), ccpp's code smaller by a
little (the TC_ sets), ccgen and ccrel unchanged in size or smaller by
a little. Speed: cccomp
parses type names with fewer ops per check. Any number that comes out
the other way is reported plainly, per AGENTS.md.

## 12. Suggested steps, for discussion

1. tokin.h, token.h, pplex.c, ccish.msg, CCPP.md: the keyword reorder,
   the T_IS_* macros, TOK_VERSION 2. One commit. Measure cccomp.
2. ir.h: the JOIN/SEL reorder (section 3.1), the IR_IS_* macros and
   the ordering-constraint comments; parse.c and irin.c use them.
   IR_VERSION 6 to 7; lower.c's and irin.c's parallel tables move the
   SEL and JOIN entries one slot.
3. token.h: the TC_STR/TC_KEYWORD move (section 4), TC_IS_WORDVAL,
   TOK_VERSION 3; ppmac.c and ppout.c use it; CCPP.md and the
   tests/pp token streams move with it.
4. mop.h: the families comment, the four OP_IS_* macros, the FIX_
   comment; peep.c uses them, and frame_expr() drops the dead INCR and
   DECR comparisons.
5. obj.h: REC_IS_PASSTHROUGH; irin.c uses it.
6. pplex.h: TC_IS_PARAM; ppmac.c uses it.
7. The .c-local items of section 9 (is_place, F_WORD..F_ENTER, the
   stale K_* comment).

Open questions:

- **Q1.** Is the TOK_VERSION bump acceptable now? It is a format change
  to an intermediate file; old NAME.i files stop reading (they are
  regenerated every run). The alternative — macros over the present
  scatter — keeps the format but gives uglier, multi-term macros and
  most of no size win (is_type_start's macro becomes eight compares
  and four ORs instead of two compares).
- **Q2.** The macro names: T_IS_TYPE_START and the rest, or a concept-
  first form like the task note's SIMPLE_TYPE_START?
- **Q3.** Define the groups no current code tests as a set (int..void,
  switch..default) as macros now, or add them when a use appears?
  Suggested: only the used ones, since each unused macro is a range
  that future changes must keep true.
- **Q4.** The JOIN/SEL swap in ir.h (section 3): suggested no.

Decided (2026-07-21, before implementation): Q1 — yes, bump
TOK_VERSION and change the intermediate formats as the code needs,
verifying the tests; Q2 — the proposed names; Q3 — only the groups
the code actually uses (refined in section 3 to "used in more than
one place"); Q4 — do the JOIN/SEL reorder now (section 3.1, one
commit with the IR macros).
