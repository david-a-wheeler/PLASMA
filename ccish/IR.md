# ccish intermediate representation (IR)

The IR is the stream of ops that the parser (stage 1, `src/parse.c`)
sends to the lowering stage (stage 2, `src/lower.c`) by calling
`ir_put(op, a, sym)`. See [approach.md](approach.md), section 4A, for
the pipeline as a whole. `ccgen -i` (and `ccish -i`) prints the
stream.

**Since 18.7 step 1** (approach.md), stages 2 to 4 are a program of
their own, `ccgen`. The parser's program, `cccomp`, writes the IR to a
file, `NAME.ir`, whose encoding is in [OBJECT.md](OBJECT.md),
"`NAME.ir`"; `ccgen` reads it, lowers each op (`lower_op()`), and
prints it (`-i`). The ops and their meanings are as below. Since step
4, the IR buffer is in `ccgen` too ("The IR buffer", below): the parser
writes `IR_SWAP` where it once exchanged blocks itself, so the file
holds the ops in the order the parser wrote them, and `-i` shows them
so.

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
   give 1, PLASMA's give `$FFFF`. (The C VM's `NOT` used to differ
   from the 6502 VMs', giving 1; now all give `$FFFF`.) Pointer arithmetic scales by element size. C-specific
   meanings stay in the IR, so the parser never has to work around VM
   quirks. Lowering maps them safely (approach.md, issues 7 and 17).
   `IR_NOT` is the first such op.
5. **Reordering.** PLASMA has no SWAP, so a store through a computed
   address, which needs the value below the address, is arranged by
   the parser. It emits the address, then the value, as C reads them,
   and then has the two blocks of IR exchanged (`IR_SWAP`, below;
   see `IR_STORE`, and approach.md 4(d2) and 18.7, A): the parser
   counts IR ops written (`ir_here()`) and gives the blocks' lengths.
   Compound assignment and `++`/`--`
   on a computed address need the address twice, so they still save
   it in a temporary local (4(e)).

## The IR buffer

`ccgen` (`src/irin.c`) doesn't lower an op as soon as it reads it. It
holds up to 128 ops, so that an `IR_SWAP` can still exchange the two
blocks of ops just before it, in place, by three reversals. Moving IR
ops is safe because each one lowers on its own, and branches name
labels by number. When the buffer is full, its older half is lowered,
so the last 64 ops are always held; at the end of a function, all of
it. A `SWAP` whose blocks have already been lowered (in part), or that
must not move code, is done in other ways (see `IR_SWAP`). `ccgen -i`
prints each op as it reads it, with the `SWAP`s, so it shows the
parser's order, not the final one.

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
- **Now also** merged with the following op in the peephole stage:
  `CB 5 ADD` → `ADDI 5`, `CN 1 ADD` → `INCR`, `CW $C000 LB` →
  `LAB $C000` (approach.md, 17, P1–P5 and P8).

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

### `IR_HEAPADDR` *sym*

Push the address of a global that may move to the heap (approach.md,
"Large zero-filled data on the heap"; OBJECT.md, "Zero-filled
globals"): a global array or struct with no initializer, of at least `BSS_MIN` (256) bytes, or an
`extern` one that may be one (of unknown size, or of at least 256). Its
function operand is the global's variable index; *a* is 0.

- **Stack:** `( -- addr )`.
- **Now:** `LA addr`, with a data symbol fixup (`FIX_HEAP`), which the
  emitter writes as a kind 4 relocation with addend 0. If the global
  moves, `ccrel` makes it `LAW` of the global's pointer word; if not, it
  stays an `LA` of the global's address.
- **Every access goes through it.** A load, store or address of such a
  global, at any offset, is `HEAPADDR`, then `CONST off ADD` if the
  offset isn't 0, then `LOAD`, `STORE` (or the byte forms) as for a
  pointer. So no other op ever holds such a global's address, and the
  peephole stage never folds the `LA` into an absolute load or store
  (`LAB`, `SAW` and the rest), which `ccrel` couldn't patch.

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
  stage makes `v LLA a IDXLW i SW` (`v LA a IDXLW i SW` for a global
  `a`, and `IDXAW i` for a global `i`). A constant index on an array variable is a variable at a
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
- In the IR, the parser writes `e v SWAP STORE` (`e v DUP SWAP STORE`
  if the value is used), and `ccgen` moves `v`'s code ahead of `e`'s.
  If the expression was too long for its IR buffer, so that `e`'s code
  has already been lowered, `ccgen` exchanges the values when the code
  runs, through its own temporaries (see `IR_SWAP`).

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
`LLA a IDXLW i DLW t LW ADDLW v LLW t SW`. That is 1 byte smaller, and
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

### The volatile access ops, `$20`-`$2B`

Each access op (`IR_LOCAL` through `IR_STOREB`) has a volatile form,
its number `| $20` (`IR_VOLATILE`, ir.h), which cccomp writes for an
object whose type is volatile: a variable, a pointer's target, an
array's element or a struct's member (which has the struct's
qualifiers). Each does what its plain form does, and lowers to the
same opcode, with the machine op's bit 0 set (`MOP_VOLATILE`) until
emit.c writes it; but no optimization that would change how often it
runs, or merge it with another, matches it, since a pattern names the
ops it rewrites and these aren't those. A pattern that keeps the
access exactly once may opt in: `DLW n DROP` to `SLW n`, `CW addr LB`
to `LAB addr`, and the fused loads (`LLW n ADD` to `ADDLW n`). cccomp
also reads a volatile pointer or index variable where it is written,
not later from a pending element, loads a volatile `x;` that it would
otherwise drop, and keeps an assignment's value with a `DUP` rather
than reading a volatile object back. (approach.md issue 25.)

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
  `DECR`, any other constant added or subtracted that fits a byte
  either way → `ADDI` or `SUBI` (`x - 5` and `x + -5` are `SUBI 5`), a
  constant from 0 to 255 then `AND` → `ANDI` (which is also how a
  conversion to `char` is done), `DUP ADD ADD` → `IDXW` (so `p + i`
  for an `int *` is `p i IDXW`), and a variable's load then `ADD` or
  `IDXW` → `ADDLW`, `IDXLW` (`ADDLB`, `IDXLB` for a `char`; `ADDAW`,
  `IDXAW`, `ADDAB`, `IDXAB` for a global), so `p + i` with a local `i`
  is `p IDXLW i`. A multiply by a constant
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
- **Now:** `NOT` then `ANDI 1`. PLASMA's `NOT` gives `$FFFF` for true
  on every VM (the C VM's used to give 1). Masking with 1 gives C's 1,
  in two ops.
- **Why it differs from `NOT`:** C's result is 1, not `$FFFF` (the VMs
  also used to disagree; reason 4).
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

### Labels and branches: `IR_LABEL`, `IR_JUMP`, `IR_BRFALSE`, `IR_BRTRUE` (*a* = label), `IR_JOIN` (*a* = label, *f* = label)

Control flow within a function. Labels are numbered per function by
the parser. `IR_LABEL` marks where a label is; the jumps and branches
go to one. `IR_JOIN a f` makes the branches so far to label *a* go to
label *f* instead.

- **Stack:**
  - `IR_LABEL`, `IR_JUMP`, `IR_JOIN`: none;
  - `IR_BRFALSE`, `IR_BRTRUE`: `( x -- )`, branching if `x` is 0 (or,
    for `IR_BRTRUE`, not 0).
- **Now:** `IR_LABEL` becomes the pseudo-op `OP_LABEL`, and `IR_JOIN`
  the pseudo-op `OP_JOIN`; neither emits bytes. The branches become
  `BRNCH`, `BRFLS` and `BRTRU`, whose operand is still the label
  number. Stage 4 turns it into a relative offset: right away for a
  label already placed (a loop's top), or through the label's fixup
  chain (approach.md 18.2) when the label is placed later. `OP_JOIN`
  moves label *a*'s chain onto label *f*'s, or resolves it at once if
  *f* is placed.
- **Why they differ:** PLASMA branches need an offset, and a forward
  target's offset isn't known yet (reason 3).
- **How the parser uses them:**
  - `if`, `do`, `?:`: see the comments on `parse_if` and the other
    functions in `src/parse.c` for the exact layouts; `while` and
    `for` write loop markers instead ("Loops", below);
  - `a && b`, `a || b` and `!` on them are jumps, not values
    (approach.md C4, "Conditions as jumps"): the result jumps to its
    false label when false and falls through when true. `a && b` is
    `a BRFALSE F b BRFALSE F`; `a || b` is `a BRTRUE T b BRFALSE F`,
    with `T` placed only when the code goes on along the true path (or
    made the false label by `!`, or joined to a `do` loop's top). An
    operand that is itself `&&` or `||` brings its own labels, reused,
    or joined with `IR_JOIN`. A condition's false label joins the
    statement's (`if`'s else). Used as a value, the result becomes
    `T: CONST 1 JUMP E F: CONST 0 E:`;
  - a label that nothing has branched to is not emitted, so that it
    doesn't stop the peephole stage from dropping unreachable code.
- **Peephole:** a `BRNCH` makes the following code unreachable until
  the next label, and labels are barriers. A join isn't: it waits in
  the window in order, and is kept in unreachable code (the branches
  it moves are live). Just before a label flushes the window, a
  `BRNCH` to that label is removed, and `Bcond L BRNCH T` then `L:`
  becomes `Binv T L:` (the opposite branch: `BRFLS`/`BRTRU`,
  `BREQ`/`BRNE`), skipping any joins in between.
- **Later:** branches to branches shortened.

### Loops: `IR_LOOP`, `IR_LOOPSTEP`, `IR_LOOPBODY`, `IR_LOOPEND` (*a*, *f* = labels)

Markers around a `while` or `for` loop's parts, which the parser writes
in source order, so that `ccgen` can put the test at the bottom
(approach.md, "Loop design"):

```
LOOP     Ltest Lbrk    the test: a true-sense branch to Lbody
LOOPSTEP Lcont Lbrk    the step (none in a while loop)
LOOPBODY Lbody Ltest   the body
LOOPEND  Lcont Lbrk
```

- **Stack:** none; the parts between them are statements and
  conditions, which leave the stack as they found it.
- **Now:** `ccgen` holds the test and the step (IR ops, in a side
  buffer, `src/irloop.c`) instead of lowering them, writes the loop's
  own jumps and labels, and lowers the held ops after the body:
  `BRNCH Ltest  Lbody: body  Lcont: step  Ltest: test  Lbrk:`. So each
  pass runs one branch. The entry `BRNCH` is left out when the test is
  only `JUMP Lbody` (`for (;;)`, `while (1)`), and each of the loop's
  labels is placed only if something branches to it. A header too big
  for the side buffer (64 ops, for all the open loops), or a loop
  nested more than 8 deep, is lowered in place, with jumps around the
  step: `Ltest: test  BRNCH Lbrk  Lcont: step  BRNCH Ltest  Lbody: body
  BRNCH Lcont  Lbrk:`. The markers name every label for that.
- **Why they aren't plain labels and jumps:** the parser reads the test
  and step before the body, and writes as it goes; the reordering is
  done in `ccgen`, which has room for it (cccomp doesn't).
- **The parser** makes the four labels, so that `break` (to `Lbrk`)
  and `continue` (to `Lcont`) in the body can branch to them, and
  writes the test as `parse_cond(Lbody, 1)`. A `for` with no
  condition has the test `JUMP Lbody`.
- **`IR_RETURN`'s *a*** is the result's words (0, 1, or a struct's
  words, up to `MAX_BYVAL` / 2), so that a `return` inside a counted
  loop can drop the loop's two values from under it.
- **Counted loops:** when a loop's test and step have a counted loop's
  shape (an `int` local stepped by a constant, compared signed with a
  constant or another local), `ccgen` holds its body too, and if the
  body qualifies (no calls, no returns, no loops, nothing that changes
  the variable or the limit), keeps the count and the limit on the
  eval stack, with PLASMA's counted-loop opcodes (`INCBRLE` and the
  rest) or `BRLT`/`BRGT` after the step (approach.md, "Loop design",
  step 2). Those branches have no IR op: `ccgen` passes them down as
  `IR_MOP` (*a* = the machine op, *f* = the label), an op number past
  `IR_NOPS` that is never in the file.

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
- **A named function called as `(*f)(x)` or `(&f)(x)`** is an
  `IR_CALL` too: only a call of a pointer's value is `IR_ICALL`.

### `IR_FUNCADDR` *sym*

Push the address of function *sym* (approach.md, "Function pointer
design"): a function's name used as a value, `fp = f` or `&f`.

- **Stack:** `( -- addr )`.
- **Now:** `LA addr` with a function relocation record, as
  `IR_CALL`'s `CALL`. The final pass resolves it the same way: to an
  internal function (RLD `$81`, redirected by the loader to the
  function's stub, which is what `ICAL` and `CALL` both jump to, so a
  pointer to a function compares equal to it wherever it was taken),
  or to an import. Taking a function's address links it, as a call
  does (`SYM_CALLED`).
- **Why it differs from `LA`:** the target is symbolic (reason 2).

### `IR_ICALL` *a* = argument count, *f* = the result's words

Call the function whose address is on top of the stack, with the *a*
arguments below it, pushed left to right.

- **Stack:** `( a1 … an addr -- result )`, with the result's *f*
  words: 0 for `void`, 1, or a struct's words, as for `IR_CALL`.
- **Now:** `ICAL`.
- **Why it differs from `ICAL`:** the counts travel with the op, as
  `IR_CALL`'s do, since nothing names the function. A function type
  is only its return type, so the parser checks neither the count nor
  the arguments' types against the function's parameters.
- **The address goes on last**, after the arguments, which C allows
  (C11 6.5.2.2p10 leaves the order of the callee and its arguments
  open). A callee whose value needs no code until then (a variable,
  an element such as `table[i]` or `s->fn`, a constant address) is
  loaded then; any other (`pick(k)(x)`) is computed first and kept in
  a temporary local meanwhile.

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

### `IR_RETURN` *a* = the result's words

Return from the current function. For an `int` function, the return
value is on top of the stack; for a `void` function, nothing is; for a
struct (returned by value), its words are. *a* says how many (version
6: 0 before).

- **Stack:** `( value -- )`, `( -- )` or `( words -- )`.
- **Now:** `LEAVE ?`. Stage 4 fills in the frame size at
  `IR_ENDFUNC`.
- **Why it differs from `LEAVE`/`RET`:** PLASMA must pick between
  `LEAVE n` and `RET` depending on the frame, which isn't known yet
  (reason 3).
- **Also:** `RET` when the frame turns out empty (see `IR_FUNC`).
- **In a counted loop:** a return inside a loop that keeps its count
  and limit on the eval stack (approach.md, "Loop design", step 2)
  must first drop the two from under its result: `ccgen` does it, with
  *a* to know how many words to keep (approach.md, "A `return` in a
  counted loop's body").

## Between stages 2 and 4: machine ops

Lowering passes *machine ops* to the peephole stage, which passes them
to stage 4 (`Mop` in `src/mop.h`). A machine op is a real PLASMA
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

## Reordering: `IR_SWAP` *a* = top block's length, *f* = the block below's length (or -1)

(Approach.md 18.7, A; since step 4.)

- **Stack:** `( x y -- y x )`, where each of `x` and `y` is the
  value left by a block of IR ops just before the `SWAP`: the last *a*
  ops, and the *f* ops before them. The top block may also leave a
  copy of its value (it ends in `IR_DUP`, for a store whose value is
  used); both copies end up below `x`. *f* = -1: the code must not
  move (a call in `e` of `x op= e` must stay before `x`'s load). *a* =
  0 (with *f* -1): the blocks aren't known, only the values (`i[p]`'s
  `i` written late, as a held constant).
- **Uses:** a store through a computed address (`*f() = e`), `i + p`
  (so that `i` is on top to be scaled), a struct copy whose source
  address came first, and `x op= e` for a pointed-to `x`.
- **Lowering (in `ccgen`, `src/irin.c`):** if *f* isn't -1 and both
  blocks are still held, exchange them in place: the `SWAP` costs
  nothing, and stays in the buffer as done, so that an enclosing block
  keeps the length the parser counted. Otherwise, with the next op:
  - **free:** before a commutative op (`ADD`, `MUL`, `AND`, `OR`,
    `XOR`, `EQ`, `NE`) nothing; before `SUB`, `SUB NEG`; before `LT`,
    `GT` (and so on): the op with its operands reversed;
  - **stash:** if the top block is still held (and holds no other
    `SWAP` that uses a temporary): `SLW t0` just before it and `LLW t0`
    after, saving the value below across it: one temporary;
  - **exchange:** `SLW t0 SLW t1 LLW t0 LLW t1` (with `DROP` and a
    second `LLW t0` when the top block left two copies): two
    temporaries.
  The temporaries are `ccgen`'s own (`FRAME_TEMP()`): stage 4 places
  them past the parser's frame when the function ends, and adds them
  to the frame size.
- **Checked** (full build only, `CCISH_CHECKS`): before a block is
  moved or stashed around, `ccgen` checks that it leaves the values
  the `SWAP` says, using each op's stack effect (the "Stack:" lines
  here); a wrong length is a damaged file (OBJECT.md, "What `ccgen`
  checks").
- **Why it differs from PLASMA:** PLASMA has no `SWAP` (issue 21).
  With one, the exchange becomes that opcode, in `ccgen` alone.

## Unsigned comparisons: `IR_ULT`, `IR_UGT`, `IR_ULE`, `IR_UGE` *a* = the right operand's block length

(Approach.md 18.7, C; since step 5.)

- **Stack:** `( x y -- x op y )`, comparing as unsigned 16-bit
  values; the result is 1 or 0, like the signed comparisons. *a* is
  the length of `y`'s code in IR ops, just before the op (0 if not
  known).
- **Lowering (in `ccgen`, `src/irin.c`):** flip both sign bits and
  compare signed. If `y`'s code is still held, `CONST $8000 XOR` goes
  just before it (flipping `x`) and after it (flipping `y`), or, if
  `y` is one constant, into that constant: `x CONST $8000 XOR y CONST
  $8000 XOR ISLT NEG`, exactly what the parser once wrote itself. If
  not, both are flipped afterward, with `y` saved in `ccgen`'s
  temporary 1 meanwhile: `CONST $8000 XOR SLW t1 CONST $8000 XOR LLW
  t1`, then the comparison.
- **Why it differs from PLASMA:** PLASMA's comparisons are signed. VM
  opcodes `ISULT` and the rest (issue 21) would make this one op, a
  change to `ccgen` alone.
- **Used for:** `<`, `>`, `<=` and `>=` between pointers, and
  `unsigned int` from M11.

## Unsigned arithmetic: `IR_UDIV`, `IR_UMOD`, `IR_USHR` *a* = 1 if the right operand is a constant

(Approach.md, "`unsigned` and helper functions design".)

- **Stack:** `( x y -- x op y )`: `/`, `%` and `>>` on `unsigned`
  operands (for `>>`, an `unsigned` left operand). *a* is 1 when `y` is
  one constant, written just before the op; 0 otherwise.
- **Lowering (in `ccgen`):** by a constant, inline, without changing
  how many ops are held (blocks' lengths count them): `USHR` by *c* is
  `SHR` then `CW (0xFFFF >> c) AND`, masking off the copies of the
  sign bit PLASMA's arithmetic `SHR` shifts in; `UDIV` by 2^n makes
  the constant *n* and does the same; `UMOD` by 2^n makes the constant
  2^n - 1 and is an `AND`. Anything else (a value known only at run
  time, or `/` and `%` by another constant) is a `CALL` to one of
  `ccgen`'s helper functions, udiv, umod or ushr (`src/helper.c`),
  which it writes into the object once each (OBJECT.md, `H`).
- **Why they differ from PLASMA:** PLASMA's `DIV`, `MOD` and `SHR` are
  signed. VM opcodes for them (issue 21) would be a change to `ccgen`
  alone.

## Adding ops

New IR ops arrive with the milestones that need them. Reordering
is `IR_SWAP` (18.7, A). The `for` increment's side buffer (M10) may add some.

Each new op gets an entry here with the same headings: stack effect,
current lowering, why it differs from PLASMA, and what it may become
later.
