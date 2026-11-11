# Enum values: assignment, set tests, and reassignment

Size and speed rest on testing sets of enum values with one or two bit
ops. A set that is a block - a power of 2 in size, starting at a
multiple of its size - tests with one AND and one compare (IN_BLOCK,
build.h), and a set a pair is, tests with one bit. Turning one value
into another (a load into its store, a signed compare into its
opposite) is one bit op, or one add of a constant. Both need the
values assigned to make the tests and turns cheap.

The assignment is not a contract, and it changes. As the sets the
compiler tests change, the values are reassigned to keep the tests and
turns one or two ops. What is the contract is each test and turn
itself: a #define, next to the values, with a _Static_assert of what
it relies on. A false assert stops the build and says what broke; a
true one generates nothing (static_assert.md), so the checks cost
nothing, in either build, host or self-hosted.

## The rule

1. A set test or a value manipulation on an enum value is a #define
   with a _Static_assert right after it: the members, samples of the
   non-members, and the alignment or shape it relies on. Examples:
   ir.h's binary ops, tokin.h's keywords, peep.c's READS_LOCAL,
   parse.c's T_INT_OR_CHAR.
2. The #define is in the .h next to the enum when more than one file
   uses it: a reassignment is then one edit, in the header, and the
   asserts say if it holds. It is file-local in the .c, next to its
   only use, when one file uses it: each macro and constant a program
   uses in its own source costs the self-hosted build an entry
   (cccomp's MAX_IDS or MAX_VARS), so a header macro used by one file
   is bigger than a define in the file.
3. Inside the #define, name the enum's values where reasonable
   (tokin.h's T_STRUCT = T_INT + 0x08, ir.h's IR_ENDFUNC = IR_FUNC +
   4, IR_U_COMPARE(op) = (op) + (IR_ULT - IR_LT)): reassigning the
   anchor then carries the block with it. A raw number in the
   _Static_assert pins the value that must not move.
4. A comparison with a named constant (tok == T_IF) names one value
   only, and is fine anywhere. A .c file may not test or transform a
   value with a raw number, or with arithmetic on named constants,
   outside a define and assert pair: no op | 0x20, no op & 0x1F, no
   tok == T_CASE + 1, no names[op - IR_ADD] without the block's
   alignment asserted. If a test or turn is hard to express against
   the names, give it a #define of its own, with the semantic name
   (emit.c's MOP_INDEX, peep.c's IS_NEG_COMP), and assert what it
   relies on.
5. tests/spec/enumvals.sh (run by make test) checks the .c files
   against 1 and 4: a hex number, or a decimal of 16 or more, in a
   bit or shift op must be a value mask (0xFF, a byte of a value;
   0x8000, a sign bit; the list is in the script), or be in a
   _Static_assert, or in a #define whose block a _Static_assert
   follows; and arithmetic on a named constant must be in such a
   pair, or be one of the existing uses listed in the script, each
   noted with where its relationship is pinned. It also checks that
   each header that defines a set test or bit manipulation on an
   enum names this file and holds a _Static_assert.

## The three kinds of values

- Assigned by this code, free to reassign: the IR ops (ir.h), the
  parser's tokens (tokin.h), the type handles and their qualifiers
  (type.h), the kinds and flag bits (type.c's TK_*, parse.h's FN_*
  and V_*, obj.sym.h's SYM_*, pstmt.h's D_*, name.h's ID_*), and
  ccgen's pseudo-ops (mop.h's OP_FUNC_BEGIN ... OP_JOIN, the odd
  numbers below $20).
- Fixed by the VM, the format, or C: the machine opcodes (mop.h's
  OP_* are PLASMA's, ../../doc/Bytecodes.md), the NAME.i stream codes
  (token.h, pplex.h; a change is a change to the format, and bumps
  the version, CCPP.md), the record tags (obj.fmt.h: their
  characters), the fixed ids (fixed.h, NAM_VERSION), and the
  one-character tokens (their C character code, which is why
  pstmt.c's T_DEFINES_TAG may use the characters' bits). The
  _Static_asserts pin the properties the code relies on (parity,
  alignment, block shape, order), which is what the VM, the format,
  and C guarantee.
- Bits of a value, not values: 0xFF takes a byte of a value, 0x8000
  its sign bit, >> 1 halves a count. They manipulate values, not
  enums, and need no define.

## Reassigning an enum's values

1. Change the enum's values (keeping its UNUSED spares marked).
2. make (and make small): the _Static_asserts that fail name the
   #defines that rely on what moved.
3. Fix those #defines, where their asserts are.
4. make test: tests/spec/tables.c checks the op-indexed tables
   (stk_effect, lw_kind, lw_op, ir_names) against the new numbers,
   and the run tests check the generated code. A format's values
   (IR_VERSION, TOK_VERSION, NAM_VERSION, OBJ_VERSION) also get
   their version bumped, and the format's doc (../IR.md,
   ../OBJECT.md, ../CCPP.md) updated.

## A new set test or value turn

1. Find or make the set's block: a power of 2 in size, starting at a
   multiple of it; its spares marked UNUSED (tokin.h's keywords: a
   block of 16, then of 8, then of 4, and of 2).
2. #define the test next to the enum, and _Static_assert it: the
   members, samples of the non-members, the block's alignment.
3. Use the #define in the .c files. If one alone uses it, put the
   #define and the assert in that file instead (rule 2).
