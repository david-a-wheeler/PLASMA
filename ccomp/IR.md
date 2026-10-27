# ccish intermediate representation (IR)

The IR is the stream of ops that the parser (stage 1, `src/parse.c`)
sends to the lowering stage (stage 2, `src/lower.c`) by calling
`ir_put(op, a, sym)`. See [approach.md](approach.md), section 4A, for
the pipeline as a whole. `ccish -i` prints the stream.

This document describes the ops that exist now (milestones 1 to 3,
and M4 so far).
It grows as milestones add ops.

## Model

The IR is close to PLASMA bytecode on purpose: it is a stack machine
with 16-bit values and the same evaluation-stack model, so most IR ops
have an obvious PLASMA counterpart. Each op is described by its effect
on that stack, written as `( before -- after )`, with the top of stack
on the right, as in [../doc/Bytecodes.md](../doc/Bytecodes.md).

Each op is a record of an opcode, one integer operand `a`, and an
optional symbol `sym` (a function).

## Why the IR isn't just PLASMA bytecode

The differences all come from one rule: **the parser decides *what* to
compute; later stages decide *how* to encode it.** Concretely, an IR op
differs from the PLASMA op it resembles for one or more of these
reasons:

1. **The encoding isn't chosen yet.** PLASMA has five ways to push a
   constant (`CN`, `MINUS1`, `CB`, `CFFB`, `CW`) and several fused
   forms (`ADDLW`, `IDXLW`, `DLW`, …). The IR has one op per
   operation, and lowering picks the cheapest encoding. That keeps
   encoding knowledge out of the parser, and keeps the parser
   independent of the target (approach.md, 4A).
2. **Addresses are symbolic.** A call names a function, not an
   address. Data is an offset into the data segment. The final layout
   isn't known until the whole file has been read. A called function
   might be defined later in the file, or never (then it's an import).
3. **Some facts are only known later.** The frame size isn't known
   until the function ends, because C can declare locals in any block.
   PLASMA's `ENTER` and `LEAVE` need it as an operand.
4. **C semantics differ from PLASMA's.** For example, C comparisons
   give 1, PLASMA's give `$FFFF`. The C VM's `NOT` differs from the
   6502 VMs. Pointer arithmetic scales by element size. C-specific
   meanings stay in the IR, so the parser never has to work around VM
   quirks. Lowering maps them safely (approach.md, issues 7 and 17).
   `IR_NOT` is the first such op.
5. **Reordering.** Later IR will include pseudo-ops (`MARK`, `SWAP2`,
   …) that tell lowering to reorder code. PLASMA has no SWAP, so this
   is how stores through pointers get their operands in the right order
   (approach.md, 4(d) and 4(e)).

## Ops

### `IR_FUNC` *sym*, *a* = parameter count

Start function *sym*. Its *a* parameters are on the evaluation stack,
first parameter deepest.

- **Stack:** `( p1 … pn -- )`, at run time, on entry.
- **Now:** a function-start marker, then `ENTER ?, n`. The frame size
  is filled in at `IR_ENDFUNC`.
- **Why it differs from `ENTER`:** `ENTER` needs the frame size up
  front, but it isn't known yet (reason 3). The marker also tells stage
  4 where the function starts, for its RLD entry.
- **Later:** if the frame size turns out to be 0, stage 4 deletes the
  `ENTER` and turns each `LEAVE 0` into `RET` (approach.md, 17, P20).

### `IR_ENDFUNC` *a* = frame size

End the current function. *a* is the total bytes of parameters and
locals (at most 255).

- **Stack:** none (compile time only).
- **Now:** a function-end marker. The peephole window is flushed, then
  stage 4 writes *a* into the function's `ENTER` and every `LEAVE` in
  its function buffer, and writes the function and its relocation
  records to the code stream ([OBJECT.md](OBJECT.md)).
- **Why it differs:** PLASMA has no equivalent. It exists because of
  reason 3.

### `IR_CONST` *a*

Push the constant *a* (taken as 16 bits).

- **Stack:** `( -- a )`.
- **Now:** the shortest encoding:
  - `$FFFF` → `MINUS1`;
  - 0–15 → `CN`;
  - 16–255 → `CB`;
  - `$FF00`–`$FFFE` → `CFFB`;
  - otherwise `CW`.

  This is the same rule as PLASMA's compiler.
- **Why it differs:** one op instead of five (reason 1).
- **Constant folding happens before the IR.** The parser keeps a
  constant *pending* until its value is needed, like a variable.
  Unary `- ~ !` on a pending constant, or a binary operator with two of
  them, is computed at compile time, so `2 * 3` reaches the IR as one
  `IR_CONST 6`. The left operand of a binary operator is held on a
  small stack in the parser, and any later IR op writes it out first,
  so the evaluation order is kept. For `c op x` with a commutative op
  and a plain variable `x`, the parser emits `x c op` instead, so that
  `1 + x` can become `INCR`.
- **Later:** merged with the following op where possible: `CB 5 ADD` →
  `ADDI 5`, `CN 1 ADD` → `INCR` (done), `CW $C000 LB` → `LAB $C000`
  (approach.md, 17, P1–P5 and P8).

### `IR_DATAADDR` *a* = data offset

Push the address of byte *a* of this module's data segment. In M1 this
is used for string literals: *a* is the first character for a C
string, or the length byte for `pstr()`.

- **Stack:** `( -- addr )`.
- **Now:** `LA addr`, plus a data relocation record. The final pass
  fills in the address once the layout is known, and adds an RLD `$81`
  entry so the loader can adjust it.
- **Why it differs from `LA`:** the target is a segment offset, not an
  address (reason 2).
- **Later:** global variables use it too. A following load or store
  fuses with it: `LA g LW` → `LAW g`, and `LA g SW` → `SAW g`. A
  following constant offset folds in: `LA g CN 4 ADD` → `LA g+4` (17,
  P7 and P9).

### `IR_LOCAL` *a* = frame offset

Push the word at frame offset *a*: a parameter or a local variable.
Parameters come first (the first at 0, the next at 2, and so on), then
locals. Sibling blocks reuse the same frame space.

- **Stack:** `( -- value )`.
- **Now:** `LLW a`. A `char` local uses `IR_LOCALB` instead.
- **Why it differs from `LLW`:** hardly at all. It's named for what it
  means, a local variable; the encoding is left to lowering.
- **Later:**
  - fused forms chosen by lowering: `ADDLW`, `IDXLW`;
  - `DLW` after a store to the same local;
  - `LLB n+1` for `(x >> 8) & 0xFF` (17, P10–P12).

### `IR_SETLOCAL` *a* = frame offset

Store the top value into the local at frame offset *a*, and **keep it**
on the stack. This is C's assignment, whose value can be used, as in
`a = b = 0` or `f(x = 1)`.

- **Stack:** `( x -- x )`.
- **Now:** `DLW a`. When the value isn't used, the parser follows it
  with `IR_DROP`, and the peephole stage turns `DLW a DROP` into
  `SLW a`. So the parser never needs to know whether an assignment's
  value will be used.
- **Why it differs from `SLW`/`DLW`:** it always keeps the value, which
  matches C's semantics. The choice of encoding is left to later stages
  (reason 1).
- **Also in the peephole stage:** `SLW n LLW n` (store, then reload)
  becomes `DLW n`.

### `IR_LOCALB` and `IR_SETLOCALB` *a* = frame offset

The same as `IR_LOCAL` and `IR_SETLOCAL`, for a `char` local: one byte
at frame offset *a*. `IR_LOCALB` pushes the byte, zero-extended (plain
`char` is unsigned, approach.md issue 7). `IR_SETLOCALB` stores the low
byte of the top value and keeps **that byte** as the value, which is
C's value for an assignment to a `char` (`c = 300` gives 44).

- **Stack:** `( -- byte )` and `( x -- x & $FF )`.
- **Now:** `LLB a` and `DLB a`. The peephole patterns for words apply
  to bytes too: `DLB a DROP` → `SLB a`, and `SLB a LLB a` → `DLB a`.
- **Why they differ from the word ops:** the parser chooses them from
  the variable's type, so they are separate ops rather than a type on
  `IR_LOCAL`; the IR has one integer operand per op.

A `char` local takes one frame byte. A `char` parameter still takes a
word, because `ENTER` pops words; it is read with `LLB` (its low byte),
which converts the argument to `char` as C requires. A `char` value on
the stack is always 0–255, so the parser converts a value to `char`
(with `CONST 255 AND`, which becomes `ANDI 255`) only for a cast or a
`char` function's return value, and not when it is already a `char`.

**How assignment is parsed:** a variable isn't loaded as soon as it's
parsed, because it may turn out to be the target of `=`, `+=` or
`++` (approach.md 4(a), option A). The parser keeps it pending and
emits `IR_LOCAL` only when the value is needed. For example:
- `x = e` is `e SETLOCAL x`;
- `x += e` is `LOCAL x e ADD SETLOCAL x`;
- `++x` is `LOCAL x CONST 1 ADD SETLOCAL x`.

### `IR_DUP`

Duplicate the top value.

- **Stack:** `( x -- x x )`.
- **Now:** `DUP`.
- **Why it differs:** it doesn't.
- **Used for:** postfix `x++`, which must yield the old value:
  `LOCAL x DUP CONST 1 ADD SETLOCAL x DROP` leaves the old value. When
  that value is unused, the peephole stage removes the copy, so the
  statement `x++;` is just `LLW x INCR SLW x`.

### Binary arithmetic: `IR_ADD`, `IR_SUB`, `IR_MUL`, `IR_DIV`, `IR_MOD`, `IR_AND`, `IR_OR`, `IR_XOR`, `IR_SHL`, `IR_SHR`

Replace the top two values with `x op y`. All are 16-bit, signed where
that matters.

- **Stack:** `( x y -- x op y )`.
- **Now:** one PLASMA op each: `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `AND`,
  `IOR`, `XOR`, `SHL`, `SHR`. Their VM semantics match C's for `int`:
  - `DIV` truncates toward zero;
  - `MOD` takes the sign of the dividend;
  - `SHR` is arithmetic, which is what gcc does for signed `>>`.
- **Why they differ:** not yet. They will carry operand types (reason 4)
  starting in M4. Then:
  - `pointer + int` scales the integer by the element size (`IDXW` for
    word elements, a multiply for structs). If the pointer is the right
    operand, the left one needs scaling, using a spill or the
    reordering pseudo-ops.
  - `unsigned` operands (M11) need different `/`, `%`, `>>` and
    comparisons.
- **Now in the peephole stage:** `CN 1 ADD` → `INCR`, `CN 1 SUB` →
  `DECR`, and a constant from 0 to 255 then `AND` → `ANDI` (which is
  also how a conversion to `char` is done).
- **Later:**
  - `ADDI`/`SUBI`/`ORI`;
  - fused forms (`ADDLW`, `IDXW`, …);
  - strength reduction;
  - `(x >> 8) & 0xFF` → a high-byte load.

  These are 17, P1–P5, P10 and P11. The parser rejects a constant zero
  divisor and a constant shift count outside 0–15, because the VMs
  disagree there (the C VM crashes on division by zero; the 6502 VMs
  give garbage and use only the count's low byte). A divisor that is
  zero only at run time is not checked.

### Unary arithmetic: `IR_NEG`, `IR_COMP`

Negate, or complement the bits of, the top value.

- **Stack:** `( x -- -x )` and `( x -- ~x )`.
- **Now:** `NEG` and `COMP`. The parser folds them on constants, so
  `-5` is a single `CFFB 251`. The peephole stage also folds them into
  a preceding constant, as a backstop for constants that only meet
  after lowering.
- **Why they differ:** they don't.

### `IR_NOT`

C's logical not: 1 if the top value is 0, otherwise 0.

- **Stack:** `( x -- !x )`.
- **Now:** `NOT` then `ANDI 1`. PLASMA's `NOT` gives `$FFFF` for true on
  the 6502 VMs but 1 on the C VM. Masking with 1 gives C's 1 on every
  VM, in two ops.
- **Why it differs from `NOT`:** C's result is 1, not `$FFFF`, and the
  VMs disagree (reason 4).
- **Now in the peephole stage:** when the result only feeds a branch,
  no mask is needed: `NOT ANDI 1 BRFLS` becomes `BRTRU`. That is safe
  because the VMs agree on which values are zero.

### Comparisons: `IR_EQ`, `IR_NE`, `IR_LT`, `IR_GT`, `IR_LE`, `IR_GE`

Compare the top two values, signed, giving C's 1 for true or 0 for
false.

- **Stack:** `( x y -- x op y )`.
- **Now:** `ISEQ`, `ISNE`, `ISLT`, `ISGT`, `ISLE` or `ISGE`, then `NEG`.
  PLASMA's comparisons give `$FFFF` for true (on every VM), and `NEG`
  turns that into 1 in one byte.
- **Why they differ:** C's true is 1 (reason 4).
- **Now in the peephole stage:** when the result only feeds a branch,
  the `NEG` goes (`NEG BRFLS` → `BRFLS`), and an equality test fuses
  with the branch: `ISEQ BRFLS` → `BRNE`, `ISNE BRFLS` → `BREQ`. A
  comparison with 0 becomes a plain test: `CN 0 BRNE` → `BRTRU`.
- **In the parser:** constants fold to 1 or 0, and `c < x` with a
  constant `c` is emitted as `x > c`, like other swappable operators.
- **Later:** `unsigned` comparisons (M11), with PLASMA's `isult` and
  friends or a sign-flip trick.

### Labels and branches: `IR_LABEL`, `IR_JUMP`, `IR_BRFALSE`, `IR_BRTRUE`, `IR_BRAND`, `IR_BROR` (*a* = label)

Control flow within a function. Labels are numbered per function by
the parser. `IR_LABEL` marks where a label is; the others branch to
one.

- **Stack:**
  - `IR_LABEL`, `IR_JUMP`: none;
  - `IR_BRFALSE`, `IR_BRTRUE`: `( x -- )`, branching if `x` is 0 (or,
    for `IR_BRTRUE`, not 0);
  - `IR_BRAND`: if `x` is 0, branch and keep it, `( x -- x )`;
    otherwise `( x -- )`. `IR_BROR` is the same with "not 0". These are
    for `&&` and `||`.
- **Now:** `IR_LABEL` becomes the pseudo-op `OP_LABEL`, which emits no
  bytes. The branches become `BRNCH`, `BRFLS`, `BRTRU`, `BRAND` and
  `BROR`, whose operand is still the label number. Stage 4 turns it into
  a relative offset: right away for a label already placed (a loop's
  top), or through the label's fixup chain (approach.md 18.2) when the
  label is placed later.
- **Why they differ:** PLASMA branches need an offset, and a forward
  target's offset isn't known yet (reason 3).
- **How the parser uses them:**
  - `if`, `while`, `do`, `for`, `?:`: see the comments on `parse_if`
    and the other functions in `src/parse.c` for the exact layouts;
  - `a && b`: `a BRAND L b' L:`, where `b'` is `b` made 1 or 0 (by
    `CONST 0 NE`) unless it already is, such as a comparison. `a || b`
    is the same with `BROR`, and `a` made 1 or 0 too;
  - a label that nothing has branched to is not emitted, so that it
    doesn't stop the peephole stage from dropping unreachable code.
- **Peephole:** a `BRNCH` makes the following code unreachable until
  the next label, and labels are barriers.
- **Later:** a `BRNCH` to the very next label can be removed, and
  branches to branches shortened. `for` loops may use a side buffer
  for the step, and PLASMA's FOR opcodes (17, P18).

### `IR_CALL` *sym*, *a* = argument count

Call function *sym* with the *a* arguments on the stack, pushed left
to right.

- **Stack:** `( a1 … an -- result )` for an `int` function,
  `( a1 … an -- )` for a `void` one.
- **Now:** `CALL addr` plus a function relocation record. The final
  pass resolves it
  either to an internal function (RLD `$81`; the loader redirects it to
  the function's stub), or, if *sym* is never defined in this file, to
  an import (RLD `$91` plus an ESD entry).
- **Why it differs from `CALL`:**
  - the target is symbolic (reason 2);
  - the argument count travels with the op, so later stages can track
    stack depth. The parser has already checked the count against the
    function's declaration or the CMDSYS table.
- **Later:** indirect calls through function pointers (`ICAL`), and
  small-struct multi-value returns (approach.md, 16).

### `IR_DROP`

Discard the top value. The parser emits it after an expression
statement whose value is unused. It never follows a `void` call,
because that leaves nothing on the stack. A statement that is just a
variable (`x;`) emits nothing at all.

- **Stack:** `( x -- )`.
- **Now:** `DROP`.
- **Why it differs:** it doesn't.
- **Now in the peephole stage:** `DLW n DROP` → `SLW n`, and
  `DUP op SLW n DROP` → `op SLW n`.
- **Later:** `DROP DROP` → `DROP2`. And when the dropped value has no
  side effects, the peephole stage can remove both the value's code
  and the `DROP`.

### `IR_RETURN`

Return from the current function. For an `int` function, the return
value is on top of the stack; for a `void` function, nothing is.

- **Stack:** `( value -- )` or `( -- )`.
- **Now:** `LEAVE ?`. Stage 4 fills in the frame size at
  `IR_ENDFUNC`.
- **Why it differs from `LEAVE`/`RET`:** PLASMA must pick between
  `LEAVE n` and `RET` depending on the frame, which isn't known yet
  (reason 3).
- **Later:**
  - `RET` when the frame turns out empty;
  - a return inside a `for` loop that uses PLASMA's FOR opcodes must
    first drop the loop's two stack values. The parser knows the loop
    nesting, so it will emit those drops (17, P18).

## Between stages 2 and 4: machine ops

Lowering passes *machine ops* to the peephole stage, which passes them
to stage 4 (`Mop` in `src/ccish.h`). A machine op is a real PLASMA
opcode, its operands, and two kinds of deferred information:

- **`FRAME_LATER`** as an `ENTER` or `LEAVE` frame-size operand: stage 4
  records where that byte is and patches it at function end.
- **A relocation** for an `LA` or `CALL` word operand. Stage 4 writes
  the word as 0 plus a relocation record in the code stream. The final
  pass (`src/link.c`) fills in the address once the layout is known,
  and writes the matching RLD or ESD entries ([OBJECT.md](OBJECT.md)).

Three pseudo-ops travel with them: `OP_FUNC_BEGIN`, `OP_FUNC_END` and
`OP_LABEL`. They produce no bytes, and they are barriers for the
peephole stage. A branch machine op's operand is a label number until
stage 4 replaces it with an offset.

## Adding ops

New IR ops arrive with the milestones that need them. Expected next:
- typed loads and stores, and addresses of variables (M4);
- the reordering pseudo-ops (M10).

Each new op gets an entry here with the same headings: stack effect,
current lowering, why it differs from PLASMA, and what it may become
later.
