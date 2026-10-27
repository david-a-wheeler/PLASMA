# ccish intermediate representation (IR)

The IR is the stream of ops that the parser (stage 1, `src/parse.c`)
sends to the lowering stage (stage 2, `src/lower.c`) by calling
`ir_put(op, a, sym)`. See [approach.md](approach.md), section 4A, for
the pipeline as a whole. `ccish -i` prints the stream.

This document describes the ops that exist now (milestone 1). It grows
as milestones add ops.

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
   M1 has no such ops yet; they arrive with M2 and M3.
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
- **Now:** a function-end marker. Stage 4 writes *a* into the
  function's `ENTER` and every `LEAVE`, then flushes the peephole
  window.
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
- **Later:** merged with the following op where possible: `CB 5 ADD` →
  `ADDI 5`, `CN 1 ADD` → `INCR`, constant folding, `CW $C000 LB` →
  `LAB $C000` (approach.md, 17, P1–P5 and P8).

### `IR_DATAADDR` *a* = data offset

Push the address of byte *a* of this module's data segment. In M1 this
is used for string literals: *a* is the first character for a C
string, or the length byte for `pstr()`.

- **Stack:** `( -- addr )`.
- **Now:** `LA addr`, plus a data relocation. Stage 4 fills in the
  address once the layout is known, and adds an RLD `$81` entry so the
  loader can adjust it.
- **Why it differs from `LA`:** the target is a segment offset, not an
  address (reason 2).
- **Later:** global variables use it too. A following load or store
  fuses with it: `LA g LW` → `LAW g`, and `LA g SW` → `SAW g`. A
  following constant offset folds in: `LA g CN 4 ADD` → `LA g+4` (17,
  P7 and P9).

### `IR_LOCAL` *a* = frame offset

Push the word at frame offset *a*. In M1 that's a parameter: the
first parameter is at 0, the next at 2, and so on.

- **Stack:** `( -- value )`.
- **Now:** `LLW a`.
- **Why it differs from `LLW`:** hardly at all yet. It's named for what
  it means, a local variable, so that later it can carry the variable's
  type. Then a `char` local becomes `LLB`.
- **Later:**
  - fused forms chosen by lowering: `ADDLW`, `IDXLW`;
  - `DLW` after a store to the same local;
  - `LLB n+1` for `(x >> 8) & 0xFF` (17, P10–P12).

### `IR_ADD`

Add the top two values.

- **Stack:** `( x y -- x+y )`.
- **Now:** `ADD`.
- **Why it differs:** not yet. Starting in M4 it will carry operand
  types (reason 4). Then `pointer + int` scales the integer by the
  element size (`IDXW` for word elements, a multiply for structs). If
  the pointer is the right operand, the left one needs scaling, using a
  spill or the reordering pseudo-ops.
- **Later:** `ADDI`, `INCR`, `ADDLW`/`ADDAW`, `IDXW`, and constant
  folding (17, P2, P3, P10).

### `IR_CALL` *sym*, *a* = argument count

Call function *sym* with the *a* arguments on the stack, pushed left
to right.

- **Stack:** `( a1 … an -- result )` for an `int` function,
  `( a1 … an -- )` for a `void` one.
- **Now:** `CALL addr` plus a function relocation. Stage 4 resolves it
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
because that leaves nothing on the stack.

- **Stack:** `( x -- )`.
- **Now:** `DROP`.
- **Why it differs:** it doesn't.
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
- **A relocation kind** (`FIX_DATA` or `FIX_FUNC`) for a word operand.
  Stage 4 fills in the address once the layout is known, and writes the
  matching RLD or ESD entries.

Two pseudo-ops travel with them: `OP_FUNC_BEGIN` and `OP_FUNC_END`.
They produce no bytes, and they are barriers for the peephole stage.

## Adding ops

New IR ops arrive with the milestones that need them. Expected next:
- labels and branches (M3);
- typed loads and stores, and addresses of variables (M2 and M4);
- the remaining arithmetic and comparison ops, with C semantics (M2
  and M3);
- the reordering pseudo-ops (M10).

Each new op gets an entry here with the same headings: stack effect,
current lowering, why it differs from PLASMA, and what it may become
later.
