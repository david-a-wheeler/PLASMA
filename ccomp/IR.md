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
5. **Reordering.** PLASMA has no SWAP, so a store through a computed
   address, which needs the value below the address, is arranged by
   the parser. It emits the address, then the value, as C reads them,
   and then exchanges the two blocks of IR while they are still in
   stage 2's buffer (`ir_rotate()`; see `IR_STORE`, and approach.md
   4(d2)). No pseudo-ops are needed: the parser keeps IR positions in
   its own variables (`ir_here()`). Compound assignment and `++`/`--`
   on a computed address need the address twice, so they still save
   it in a temporary local (4(e)).

## Stage 2's buffer

`ir_put()` doesn't lower an op at once. It holds up to 64 ops, so that
the parser can still exchange two adjacent blocks of them
(`ir_rotate(m0, m1)` turns `[m0, m1) [m1, end)` into
`[m1, end) [m0, m1)` in place, by three reversals). Moving IR ops is
safe because each one lowers on its own, and branches name labels by
number. The buffer is lowered at the end of each full expression
(`ir_flush()`), at the end of each function, and when it is full.
After that, `ir_holds()` tells the parser that the code can't move,
and the parser falls back to temporaries. `ccish -i` prints each op as
it is lowered, so it shows the final order.

## Ops

### `IR_FUNC` *sym*, *a* = parameter words

Start function *sym*. Its parameters' *a* words are on the evaluation
stack, first parameter deepest: one word for each scalar, and a struct
passed by value as the words of its bytes (approach.md, "M6 step 3
design").

- **Stack:** `( p1 … pn -- )`, at run time, on entry.
- **Now:** a function-start marker, then `ENTER ?, n`. The frame size
  is filled in at `IR_ENDFUNC`.
- **Why it differs from `ENTER`:** `ENTER` needs the frame size up
  front, but it isn't known yet (reason 3). The marker also tells stage
  4 where the function starts, for its RLD entry.
- **Also:** if the frame size turns out to be 0 (no parameters, locals
  or temporaries), stage 4 deletes the `ENTER` and turns each `LEAVE`
  into `RET`, as PLASMA's compiler does (approach.md, 17, P20). Since
  the `ENTER` is first, removing it moves all of the code equally, so
  branch offsets stay valid. A `LEAVE` in the middle of the function
  becomes `RET` plus a pad byte that never runs (another `RET`), so
  nothing after it moves; a final `LEAVE` becomes just `RET`.

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

Push the address of byte *a* of the module's globals: the address of a
global variable (`&g`), or a global array used as a value.

- **Stack:** `( -- addr )`.
- **Now:** `LA addr`, plus a data relocation record. The final pass
  fills in the address once the layout is known, and adds an RLD `$81`
  entry so the loader can adjust it.
- **Why it differs from `LA`:** the target is a segment offset, not an
  address (reason 2).
- **Loads and stores of globals don't use it.** They have their own
  ops (`IR_GLOBAL` and the rest, below), so nothing needs to fuse
  `LA g LW` into `LAW g`. A constant offset is folded in by the parser:
  `&a[3]` is `DATAADDR a+6`.
- **An `extern` global** with no storage in this file yet has no data
  offset. Then the op's function operand is the global's variable
  index *v*, and *a* is the addend: `&ext[3]` is `DATAADDR sym`*v*`+6`
  in the `-i` listing. It is lowered with a data symbol fixup
  (`FIX_DSYM`), which the emitter writes as a kind 4 relocation, with
  the addend in the word (OBJECT.md). This applies to `IR_GLOBAL` and
  the rest too.

### `IR_STRADDR` *a* = string pool offset

Push the address of byte *a* of the string pool, which holds every
string literal ([OBJECT.md](OBJECT.md)): *a* is the first character for
a C string, or the length byte for `pstr()`.

- **Stack:** `( -- addr )`.
- **Now:** `LA addr`, plus a string relocation record, filled in and
  given an RLD entry by the final pass, as for `IR_DATAADDR`.
- **Why it differs from `IR_DATAADDR`:** the pool is a separate stream,
  placed after the globals, so that string literals in a global's
  initializer don't land in the middle of the global. Its offsets
  aren't data offsets until the final pass knows the globals' size.

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

### `IR_LOCALADDR` *a* = frame offset

Push the address of the local at frame offset *a*: C's `&x`, and the
address of a local array (its value in C, a pointer to its first
element).

- **Stack:** `( -- addr )`.
- **Now:** `LLA a`.
- **Why it differs:** it doesn't.

### `IR_GLOBAL`, `IR_SETGLOBAL`, `IR_GLOBALB` and `IR_SETGLOBALB` *a* = data offset

The same as `IR_LOCAL`, `IR_SETLOCAL`, `IR_LOCALB` and `IR_SETLOCALB`,
for a global variable at offset *a* of the data segment, or at *a*
bytes past an `extern` global (as for `IR_DATAADDR`). A global without
an initializer is zero when the module loads: zero bytes in the data
segment, in declaration order.

- **Stack:** as for the local ops.
- **Now:** `LAW`, `DAW`, `LAB` and `DAB`, with the address relocated
  as for `IR_DATAADDR`. The peephole patterns for locals apply to
  them: `DAW a DROP` → `SAW a`, and `SAW a LAW a` → `DAW a` (also for
  bytes). `DAB` keeps the stored byte on every VM, as `DLB` does.
- **Why they differ from `IR_DATAADDR` then `IR_LOAD`:** the parser
  knows a global's address at compile time, as it knows a local's frame
  offset, so it handles both the same way. This keeps the load or store
  a single op without a fusing pattern, and the patterns above carry
  over from locals.

### `IR_LOAD` and `IR_LOADB`

Replace an address with the word (or, for a `char`, the zero-extended
byte) stored there: C's `*p` used as a value.

- **Stack:** `( addr -- value )`.
- **Now:** `LW` and `LB`.
- **Why they differ:** they don't.
- **Later:** `LLW p LW` could become a single op if PLASMA had one (it
  doesn't). Globals already load with `IR_GLOBAL` (`LAW`).

### `IR_STORE` and `IR_STOREB`

Store a word (or a value's low byte) at an address. The address is on
top, as in PLASMA, and nothing is left on the stack.

- **Stack:** `( value addr -- )`.
- **Now:** `SW` and `SB`.
- **Why they differ from `IR_SETLOCAL`:** they don't keep the value,
  because PLASMA's `SW` and `SB` don't, and there is no SWAP to arrange
  a copy cheaply.

**How stores through pointers are parsed.** `*e` is an lvalue whose
address is computed by `e`. A streaming parser has already emitted `e`
when it sees `=`, but the store needs the value first:
- If `e` is a plain pointer variable `p`, nothing has been emitted yet,
  and `*p = v` is just `v LOCAL p STORE` (`v GLOBAL p STORE` for a
  global `p`).
- An array element whose address comes only from variables and
  constants is pending too: the array is an array or pointer variable,
  local or global, and the index is a variable (local or global) or a
  constant. So `a[i] = v` is
  `v LOCALADDR a LOCAL i CONST 2 MUL ADD STORE`, which the peephole
  stage makes `v LLA a LLW i IDXW SW` (`v LA a LLW i IDXW SW` for a
  global `a`). A constant index on an array variable is a variable at a
  fixed offset: `a[2] = v` is `v SETLOCAL a+4` (`SETGLOBAL a+4`, which
  becomes `SAW`). A row of a two-dimensional array takes a constant
  index the same way (`m[1][i]`, `m[i][2]`).
- Otherwise the address is on the stack: its code (`e`) has been
  emitted. The parser emits `v` after it, then exchanges the two
  blocks, so `*e = v` is `v e STORE` with no temporary (approach.md
  4(d2)). For example, `*d++ = *s++` is
  `s++ LOADB d++ STOREB`, and `a[i + 1] = v` is
  `v LOCALADDR a LOCAL i CONST 1 ADD CONST 2 MUL ADD STORE`. The
  exchange and the store wait until the parser sees whether the
  assignment's value is used; if it is, a `DUP` goes after `v` first,
  so the value stays below the address: `v DUP e STORE`.
- If the expression was too long for stage 2's buffer, so that `e`'s
  code has already been lowered, the parser swaps after the fact with
  two temporary locals: `e v SETLOCAL t1 DROP SETLOCAL t2 DROP LOCAL
  t1 LOCAL t2 STORE`. Temporaries are freed at the end of each full
  expression.

An assignment's value is the object's new value. For a local it is
kept on the stack (`DLW`). For an object reached through a pending
address (`*p`, `a[i]`), it is read back (`LOCAL p LOAD`) only if the
value is used, so `*p = v;` as a statement costs nothing extra. For one
reached through a computed address, it is the `DUP` above, with `CONST
255 AND` when an `int` is stored into a `char`. Compound assignment and
`++`/`--`
load through the same address before the store, as in
`LOCAL p LOAD CONST 1 ADD LOCAL p STORE` for `++*p`. For a pending
array element, `++`/`--` compute its address twice. A compound
assignment to an element with an index variable saves the address in a
temporary instead, so it is computed once: `a[i] += v` becomes
`LLA a LLW i IDXW DLW t LW v ADD LLW t SW`. That is 1 byte smaller, and
it stays right if `v` changes `i` through a pointer. A compound
assignment to a pending element without an index variable (`*p op= v`,
`p[3] op= v`) computes the address twice. C evaluates the lvalue once,
so if `v` has a call (which may change `p`), the load comes after the
call, and both addresses follow it. The operator is then applied with
its operands reversed: `*p += f()` is `f() LOCAL p LOAD ADD LOCAL p
STORE`; `-=` ends `SUB NEG`; and `/=`, `%=`, `<<=` and `>>=` save
`f()`'s value in a temporary first. Without a call, the load comes
first, as usual (approach.md, "M5 step 4 is done"). (With a SWAP opcode,
neither the second copy nor the temporary would be needed: approach.md
issue 21.)

A load through a pointer whose value is unused (`*p;` or `(void)*p;`)
is still done, since on an Apple II reading some addresses (such as
`$C010`) has an effect.

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
- **Why they differ:** they don't. Pointer arithmetic is done by the
  parser with plain ops, since it knows the operand types:
  - `p + i` and `p - i` multiply `i` by the element size first
    (`CONST n MUL`; for a constant `i`, at compile time). For `i + p`,
    `i` is already below `p`, so `i`'s code is moved after `p`'s and
    scaled there: `p i CONST 2 MUL ADD`, which becomes `p i IDXW`
    (approach.md 4(d2)). If `i`'s code was too long for stage 2's
    buffer, `p` is saved in a temporary local while `i` is scaled
    (4(b)).
  - `p - q` subtracts, then divides by the element size (`CONST 1
    SHR` for 2-byte elements).
  - `unsigned` operands (M11) will need different `/`, `%`, `>>` and
    comparisons.
- **Now in the peephole stage:** `CN 1 ADD` → `INCR`, `CN 1 SUB` →
  `DECR`, a constant from 0 to 255 then `AND` → `ANDI` (which is also
  how a conversion to `char` is done), and `DUP ADD ADD` → `IDXW` (so
  `p + i` for an `int *` is `p i IDXW`). A multiply by a constant
  becomes cheaper ops, as in PLASMA's own optimizer (`codeopt.pla`),
  since `MUL` is a 16-step loop on the 6502 VMs:
  - `* 2` → `DUP ADD`. It is the same size as `CN 1 SHL` (2 bytes) but
    faster on the 6502 VM, about 52 cycles to 59, because `SHL` is a
    general shifter (it saves Y, checks for counts of 8 or more, and
    loops). It also fuses into `IDXW`. PLASMA's optimizer even turns
    `CN 1 SHL` into `DUP ADD`.
  - `* 3` → `DUP IDXW`; `* 1` → nothing; `* 0` → `DROP CN 0` (the
    operand is still computed, for its side effects);
  - `* 2^n` for n from 2 to 15 → `CN n SHL`.

  Division by a power of two is **not** turned into `SHR`, as PLASMA's
  optimizer does: `SHR` rounds toward minus infinity, but C's `/`
  truncates toward zero (`-3 / 2` is -1, `-3 >> 1` is -2). That is
  only valid for `unsigned` (M11).
- **Later:**
  - `ADDI`/`SUBI`/`ORI`;
  - fused forms (`ADDLW`, `IDXLW`, …);
  - other strength reduction;
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
  Pointers are compared **unsigned**, since addresses at or above
  `$8000` are normal: for `< > <= >=`, the parser flips the sign bit of
  each side (`CONST $8000 XOR`, folded into a constant right side)
  before the signed comparison. `==` and `!=` need no change.
- **Later:** `unsigned` integers (M11) can use the same sign flip.

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

### `IR_SEL` *a* = label, and the case table: `IR_CASES` *a* = count, `IR_CASEVAL` *a* = value, `IR_CASETO` *a* = label

A `switch` (approach.md, "M7 design"). `IR_SEL` takes the value and
goes to the case table at label *a*. The table comes after the
switch's body: the label, `IR_CASES` with the number of cases, then
for each case, sorted by value, `IR_CASEVAL` (its value) and
`IR_CASETO` (the label of its code). The code after the table runs
when no case matches.

- **Stack:** `IR_SEL`: `( x -- )`; the table ops: none (they are data).
- **Now:** `IR_SEL` becomes `SEL`, whose operand, like a branch's, is a
  label number until stage 4 makes it an offset. The table ops become
  the pseudo-ops `OP_CASES`, `OP_CASEVAL` and `OP_CASETO`, which stage 4
  writes as the table's bytes: the count byte, then each case's value
  word and offset word. The offset is relative to its own word, so it
  uses the branches' fixup chains.
- **Why they differ from `SEL`:** `SEL`'s table is data inside the
  code, which only stage 4 can lay out, and its offsets point forward
  and back to labels (reason 3).
- **Peephole:** `SEL` makes the following code unreachable until the
  next label (code before a switch's first case can't run), and the
  table ops are barriers, so the code after the table is reachable.
- **Listing:** the final pass lists the table where a `SEL` points, as
  `CASES`, `CASE` and `TO` lines.

### `IR_CALL` *sym*, *a* = argument count

Call function *sym* with the *a* arguments on the stack, pushed left
to right.

- **Stack:** `( a1 … an -- result )` for an `int` function,
  `( a1 … an -- )` for a `void` one, and `( a1 … an -- w1 … wk )` for
  one returning a struct of *k* words. A struct argument is its words
  too, so the stack holds more words than *a* then.
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
- **Later:** indirect calls through function pointers (`ICAL`).

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
- **Also:** `RET` when the frame turns out empty (see `IR_FUNC`).
- **Later:** a return inside a `for` loop that uses PLASMA's FOR
  opcodes must first drop the loop's two stack values. The parser knows
  the loop nesting, so it will emit those drops (17, P18).

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

Pseudo-ops travel with them: `OP_FUNC_BEGIN`, `OP_FUNC_END` and
`OP_LABEL`, which produce no bytes, and a case table's `OP_CASES`,
`OP_CASEVAL` and `OP_CASETO`, which produce its bytes but no opcode. All
are barriers for the peephole stage. A branch machine op's operand is a label number until
stage 4 replaces it with an offset.

## Adding ops

New IR ops arrive with the milestones that need them. Reordering
needs none: it is done by exchanging blocks in stage 2's buffer (M5
step 4). The `for` increment's side buffer (M10) may add some.

Each new op gets an entry here with the same headings: stack effect,
current lowering, why it differs from PLASMA, and what it may become
later.
