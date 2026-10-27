# PLASMA VM Bytecodes

This document describes the PLASMA virtual machine's bytecode in enough
detail to write a compiler that targets it. It covers the machine model,
every opcode with its operands and stack effect, the conventions a compiler
must follow (calls, frames, branches, `WHEN` tables, `FOR` loops), and the
relocatable module file format the loaders accept.

It describes PLASMA 2.x as found in this repository. The opcode table is the
same in every VM in the tree:

| VM | Source |
|----|--------|
| Apple 1 + CFFA1 | `src/vmsrc/apple/plvm01.s` |
| Apple II, 64K | `src/vmsrc/apple/plvm02.s` |
| Apple II, 128K | `src/vmsrc/apple/plvmjit02.s` |
| Apple II, 65802/65816 | `src/vmsrc/apple/plvm802.s` |
| Apple /// | `src/vmsrc/apple/plvm03.s`, `plvmjit03.s` |
| Commodore 64 | `src/vmsrc/c64/plvmc64.s` |
| Portable C VM (`plvm`, runs on the host) | `src/vmsrc/plvm.c`, `cmdsys.c` |

The VMs agree on what every opcode does. They differ internally, for
example in how frames are saved, and the C VM has a few real behavioral
differences. See [Significant discrepancies](#significant-discrepancies).
Where the C VM disagrees, the 6502 behavior is treated as authoritative.

## Machine model

* **Values** are 16-bit words. Arithmetic is two's complement and wraps.
  Comparisons and the fused compare/branch opcodes are **signed**. There are
  no unsigned comparison opcodes. CMDSYS provides `isugt`, `isuge`, `isult`
  and `isule` as callable functions.
* **Booleans:** comparison opcodes produce `$FFFF` (-1) for true and `$0000`
  for false, in every VM, the C VM included. Conditional branches treat any
  nonzero value as true. The one exception is logical `NOT` (`$80`): the C
  VM returns `1` rather than `$FFFF` for true. See
  [Significant discrepancies](#significant-discrepancies).
* **Byte order** of all multi-byte values (operands and memory) is
  little-endian.
* **Bytes** loaded from memory are zero-extended to 16 bits. Byte stores
  write the low 8 bits.

### Registers and stacks

| Name | Purpose |
|------|---------|
| IP   | Instruction pointer into the bytecode stream. |
| Evaluation stack | Operand stack of 16-bit words, **32 entries** deep in both the 6502 VMs and the C VM. On the 6502 it lives in zero page (`ESTKL`/`ESTKH`, indexed by the X register). The 65802 VM keeps it on the hardware stack internally. Nothing checks for overflow, so keep expression depth well under 32. |
| IFP  | Frame pointer for the current function's locals, addressed as `IFP + offset` with a 1-byte offset. A frame is at most 255 bytes (`ENTER`'s size operand is one byte). A **word** local must be at offset 254 or lower: on the 6502 its high byte at offset+1 wraps around to offset 0 instead of reaching offset 256. |
| PP   | String pool pointer. Used by `CS` in VMs that run bytecode from outside main memory, and by some VMs to allocate frames. See `CS` and [Frames](#frames). |
| 6502 hardware stack | Return addresses. `CALL`/`ICAL` are implemented with 6502 `JSR`, and `LEAVE`/`RET` with `RTS`. Each nested bytecode call uses about 4 bytes of this 256-byte stack, plus 2 more for `ENTER` on VMs that save IFP there. That limits recursion depth to roughly 40–60 levels. |

Notation in the tables below:

* **TOS** = top of stack; **NOS** = next on stack (the one under TOS).
* Stack effects are written `( before -- after )` with the top of stack at
  the **right**, e.g. `( a b -- a+b )` means `b` was TOS.
* `b` = 1-byte unsigned operand, `w` = 2-byte little-endian operand,
  `rel` = 2-byte signed branch offset (see [Branches](#branches)),
  `addr` = 2-byte absolute address (needs a relocation entry in a module).
* `L[o]` = byte(s) at local frame offset `o` (i.e. memory at `IFP+o`).

## Opcode summary

All opcodes are **even** numbers; odd values are illegal. This makes dispatch
fast. Each entry in the handler table is a 2-byte address, so an even opcode
is already the byte offset of its entry. The 6502 inner loop (in zero page)
copies the opcode byte straight into the low byte of a `JMP (OPTBL)`
instruction's operand and executes that indirect jump, with no shifting or
address arithmetic. `OPTBL` is page-aligned and 128 entries fill exactly one
page, so the high byte never changes. That is also why opcodes stop at `$FE`.
Opcodes above `$C4` are undefined.

| | +0 | +2 | +4 | +6 | +8 | +A | +C | +E |
|-|----|----|----|----|----|----|----|----|
| **$00** | CN 0 | CN 1 | CN 2 | CN 3 | CN 4 | CN 5 | CN 6 | CN 7 |
| **$10** | CN 8 | CN 9 | CN 10 | CN 11 | CN 12 | CN 13 | CN 14 | CN 15 |
| **$20** | MINUS1 | BREQ | BRNE | LA | LLA | CB | CW | CS |
| **$30** | DROP | DROP2 | DUP | DIVMOD | ADDI | SUBI | ANDI | ORI |
| **$40** | ISEQ | ISNE | ISGT | ISLT | ISGE | ISLE | BRFLS | BRTRU |
| **$50** | BRNCH | SEL | CALL | ICAL | ENTER | LEAVE | RET | CFFB |
| **$60** | LB | LW | LLB | LLW | LAB | LAW | DLB | DLW |
| **$70** | SB | SW | SLB | SLW | SAB | SAW | DAB | DAW |
| **$80** | NOT | ADD | SUB | MUL | DIV | MOD | INCR | DECR |
| **$90** | NEG | COMP | AND | IOR | XOR | SHL | SHR | IDXW |
| **$A0** | BRGT | BRLT | INCBRLE | ADDBRLE | DECBRGE | SUBBRGE | BRAND | BROR |
| **$B0** | ADDLB | ADDLW | ADDAB | ADDAW | IDXLB | IDXLW | IDXAB | IDXAW |
| **$C0** | NATV | JUMPZ | JUMP | | | | | |

## Opcode reference

"Size" is the total instruction length in bytes, including the opcode.

### Constants

| Op | Name | Operands | Size | Stack effect | Description |
|----|------|----------|------|--------------|-------------|
| $00–$1E | CN n | — | 1 | `( -- n )` | Push the constant `n = opcode/2` (0–15). `$00` is zero. |
| $20 | MINUS1 | — | 1 | `( -- -1 )` | Push `$FFFF`. |
| $2A | CB | b | 2 | `( -- b )` | Push byte `b`, zero-extended (`$00bb`). |
| $5E | CFFB | b | 2 | `( -- $FFbb )` | Push byte `b` with high byte `$FF`, i.e. -256..-1. |
| $2C | CW | w | 3 | `( -- w )` | Push word `w`. |
| $26 | LA | addr | 3 | `( -- addr )` | Push an address. Same as `CW` at run time; use it when the operand is relocated, which also makes listings clearer. |
| $28 | LLA | b | 2 | `( -- IFP+b )` | Push the address of the local at frame offset `b`. |
| $2E | CS | len, chars… | 2+len | `( -- straddr )` | In-line string literal: a length byte followed by `len` characters (a length-prefixed string). IP continues after the last character. If the bytecode is in main memory (64K Apple II, Apple 1), this pushes the address of the length byte inside the bytecode. If the bytecode runs from elsewhere (128K Apple II auxiliary memory, Apple ///, 65802, the C VM), the string is first copied into the string pool and the copy's address is pushed. An identical string already in the current pool is reused. The pool is released when the function returns. **To be portable, treat a `CS` address as valid only until the current function returns, and never write through it.** |

Constant selection used by the PLASMA compiler (`emit_const` in
`src/toolsrc/codegen.c`): `$FFFF` → `MINUS1`; 0–15 → `CN`; `$0010–$00FF` →
`CB`; `$FF00–$FFFE` → `CFFB`; anything else → `CW`.

### Stack manipulation

| Op | Name | Operands | Size | Stack effect | Description |
|----|------|----------|------|--------------|-------------|
| $30 | DROP | — | 1 | `( a -- )` | Discard TOS. |
| $32 | DROP2 | — | 1 | `( a b -- )` | Discard two values. |
| $34 | DUP | — | 1 | `( a -- a a )` | Duplicate TOS. |

There are no SWAP, OVER or ROT opcodes. Use locals for anything more complex.

### Arithmetic and logic

All take their operands from the stack and push one result, except DIVMOD.

| Op | Name | Operands | Size | Stack effect | Description |
|----|------|----------|------|--------------|-------------|
| $82 | ADD | — | 1 | `( a b -- a+b )` | Add. The compiler also uses it to index byte arrays (IDXB). |
| $84 | SUB | — | 1 | `( a b -- a-b )` | Subtract TOS from NOS. |
| $86 | MUL | — | 1 | `( a b -- a*b )` | Multiply (low 16 bits). |
| $88 | DIV | — | 1 | `( a b -- a/b )` | Signed divide, truncating toward zero. Division by zero is not trapped: the 6502 VMs return a garbage value, and the C VM crashes. |
| $8A | MOD | — | 1 | `( a b -- a%b )` | Signed remainder; its sign follows the dividend `a`. Division by zero as for DIV. |
| $36 | DIVMOD | — | 1 | `( a b -- a/b a%b )` | Quotient and remainder; **remainder is TOS**. Not emitted by the current compilers. The C VM pushes them in the opposite order; see [Significant discrepancies](#significant-discrepancies). |
| $8C | INCR | — | 1 | `( a -- a+1 )` | Increment. |
| $8E | DECR | — | 1 | `( a -- a-1 )` | Decrement. |
| $90 | NEG | — | 1 | `( a -- -a )` | Two's-complement negate. |
| $92 | COMP | — | 1 | `( a -- ~a )` | Bitwise complement. |
| $94 | AND | — | 1 | `( a b -- a&b )` | Bitwise AND. |
| $96 | IOR | — | 1 | `( a b -- a\|b )` | Bitwise inclusive OR. |
| $98 | XOR | — | 1 | `( a b -- a^b )` | Bitwise exclusive OR. |
| $9A | SHL | — | 1 | `( a n -- a<<n )` | Shift left by `n`. The 6502 VMs use only `n`'s low byte. For portability keep `n` in 0–15. |
| $9C | SHR | — | 1 | `( a n -- a>>n )` | **Arithmetic** (sign-propagating) shift right by `n`. Same limits on `n` as SHL. There is no logical shift right; for one, AND off the sign-extended bits afterward. |
| $9E | IDXW | — | 1 | `( a i -- a+2*i )` | Index a word array: add twice TOS to NOS. |
| $80 | NOT | — | 1 | `( a -- !a )` | Logical NOT: `$FFFF` if `a` is 0, otherwise 0. The C VM pushes 1 instead of `$FFFF`; see [Significant discrepancies](#significant-discrepancies). |
| $38 | ADDI | b | 2 | `( a -- a+b )` | Add unsigned immediate byte. |
| $3A | SUBI | b | 2 | `( a -- a-b )` | Subtract unsigned immediate byte. |
| $3C | ANDI | b | 2 | `( a -- a&b )` | AND with immediate byte (result high byte is 0). |
| $3E | ORI | b | 2 | `( a -- a\|b )` | OR with immediate byte (high byte unchanged). |

### Comparisons

All are signed, pop two values, and push `$FFFF` (true) or `0` (false).

| Op | Name | Size | Stack effect |
|----|------|------|--------------|
| $40 | ISEQ | 1 | `( a b -- a==b )` |
| $42 | ISNE | 1 | `( a b -- a<>b )` |
| $44 | ISGT | 1 | `( a b -- a>b )` |
| $46 | ISLT | 1 | `( a b -- a<b )` |
| $48 | ISGE | 1 | `( a b -- a>=b )` |
| $4A | ISLE | 1 | `( a b -- a<=b )` |

### Loads

| Op | Name | Operands | Size | Stack effect | Description |
|----|------|----------|------|--------------|-------------|
| $60 | LB | — | 1 | `( addr -- byte )` | Load byte from address on TOS. |
| $62 | LW | — | 1 | `( addr -- word )` | Load word from address on TOS. |
| $64 | LLB | b | 2 | `( -- L[b] )` | Load local byte at frame offset `b`. |
| $66 | LLW | b | 2 | `( -- L[b] )` | Load local word at frame offset `b`. |
| $68 | LAB | addr | 3 | `( -- byte )` | Load byte from absolute address. |
| $6A | LAW | addr | 3 | `( -- word )` | Load word from absolute address. |

### Stores

The "S" forms pop the value. The "D" (duplicate) forms store TOS and leave it
on the stack, which supports chained assignment and `FOR` loop variables. The
byte "D" forms also clear the high byte of the value left on the stack, so it
equals what was stored.

| Op | Name | Operands | Size | Stack effect | Description |
|----|------|----------|------|--------------|-------------|
| $70 | SB | — | 1 | `( val addr -- )` | Store low byte of `val` at `addr`. Note: the **address is TOS**. |
| $72 | SW | — | 1 | `( val addr -- )` | Store word `val` at `addr` (address is TOS). |
| $74 | SLB | b | 2 | `( val -- )` | Store byte to local frame offset `b`. |
| $76 | SLW | b | 2 | `( val -- )` | Store word to local frame offset `b`. |
| $78 | SAB | addr | 3 | `( val -- )` | Store byte to absolute address. |
| $7A | SAW | addr | 3 | `( val -- )` | Store word to absolute address. |
| $6C | DLB | b | 2 | `( val -- val&$FF )` | Store byte to local `b`, keep value. |
| $6E | DLW | b | 2 | `( val -- val )` | Store word to local `b`, keep value. |
| $7C | DAB | addr | 3 | `( val -- val&$FF )` | Store byte to absolute address, keep value. |
| $7E | DAW | addr | 3 | `( val -- val )` | Store word to absolute address, keep value. |

### Fused add/index with memory operand

These combine a load with an add, for array indexing and `x + var`.

| Op | Name | Operands | Size | Stack effect |
|----|------|----------|------|--------------|
| $B0 | ADDLB | b | 2 | `( a -- a + L[b] byte )` |
| $B2 | ADDLW | b | 2 | `( a -- a + L[b] word )` |
| $B4 | ADDAB | addr | 3 | `( a -- a + byte[addr] )` |
| $B6 | ADDAW | addr | 3 | `( a -- a + word[addr] )` |
| $B8 | IDXLB | b | 2 | `( a -- a + 2 * L[b] byte )` |
| $BA | IDXLW | b | 2 | `( a -- a + 2 * L[b] word )` |
| $BC | IDXAB | addr | 3 | `( a -- a + 2 * byte[addr] )` |
| $BE | IDXAW | addr | 3 | `( a -- a + 2 * word[addr] )` |

### Branches

<a name="branches"></a>Branch operands are a signed 16-bit offset **relative
to the address of the offset word itself** (the byte right after the
opcode). If the branch is taken, `IP = address_of_offset + offset`. If it is
not taken, execution continues after the offset word. In assembler syntax,
the compiler emits `!WORD target-*` right after the opcode byte. Relative
branches need no relocation.

| Op | Name | Operands | Size | Stack effect | Branches when |
|----|------|----------|------|--------------|---------------|
| $50 | BRNCH | rel | 3 | `( -- )` | Always. |
| $4C | BRFLS | rel | 3 | `( a -- )` | `a == 0`. |
| $4E | BRTRU | rel | 3 | `( a -- )` | `a != 0`. |
| $22 | BREQ | rel | 3 | `( a b -- )` | `a == b`. |
| $24 | BRNE | rel | 3 | `( a b -- )` | `a != b`. |
| $AC | BRAND | rel | 3 | `( a -- a )` taken / `( a -- )` not taken | Short-circuit AND: if `a == 0`, branch and **leave** `a` (the result); otherwise drop `a` and fall through to evaluate the right-hand side. |
| $AE | BROR | rel | 3 | `( a -- a )` taken / `( a -- )` not taken | Short-circuit OR: if `a != 0`, branch and leave `a`; otherwise drop it and fall through. |
| $C4 | JUMP | addr | 3 | `( -- )` | Always, to an **absolute** address (used by compiled Forth). Needs relocation. |
| $C2 | JUMPZ | addr | 3 | `( a -- )` | To an absolute address when `a == 0` (compiled Forth). Needs relocation. |

Both paths out of BRAND/BROR leave exactly one value, because the
right-hand side pushes its own result when the branch is not taken.

#### FOR-loop branches

These do **not** pop the loop limit or counter. The stack holds
`( limit count )` with `count` as TOS for the whole loop, and the compiler
cleans it up after the loop. `step` is popped where present. All
comparisons are signed.

| Op | Name | Operands | Size | Stack effect | Branches when |
|----|------|----------|------|--------------|---------------|
| $A0 | BRGT | rel | 3 | `( limit count -- limit count )` | `count > limit` (skip an empty `TO` loop). |
| $A2 | BRLT | rel | 3 | `( limit count -- limit count )` | `count < limit` (skip an empty `DOWNTO` loop). |
| $A4 | INCBRLE | rel | 3 | `( limit count -- limit count+1 )` | after incrementing, `count <= limit`. |
| $A6 | ADDBRLE | rel | 3 | `( limit count step -- limit count+step )` | after adding, `count <= limit`. |
| $A8 | DECBRGE | rel | 3 | `( limit count -- limit count-1 )` | after decrementing, `count >= limit`. |
| $AA | SUBBRGE | rel | 3 | `( limit count step -- limit count-step )` | after subtracting, `count >= limit`. |

The code shape the PLASMA compiler uses for `FOR v = from TO limit [STEP s]`
(from `parse.c`):

```
        <limit>  <from>            ; ( limit count )
        BRGT    exit               ; BRLT for DOWNTO
top:    DLW/DLB/DAW/DAB v          ; v = count, keep count
        <body>                     ; must leave the stack as it found it
cont:   INCBRLE top                ; or <s> ADDBRLE / DECBRGE / <s> SUBBRGE
exit:   SLW/SLB/SAW/SAB v          ; v = final count (pops it)
        DROP                       ; drop the limit
```

`BREAK` branches to `exit` and `CONTINUE` to `cont`. Because the loop keeps
two values on the stack, a `RETURN` from inside the loop must first pop them
(the compiler emits `DROP2` for each enclosing `FOR`). The counter wraps, so
a loop whose limit is `$7FFF` (`TO`) or `$8000` (`DOWNTO`) never ends.

#### SEL (WHEN/IS multi-way branch)

| Op | Name | Operands | Size | Stack effect |
|----|------|----------|------|--------------|
| $52 | SEL | rel | 3 | `( val -- )` |

`SEL` pops `val` and follows `rel` (relative to the offset word, like any
branch) to a **case table** with this layout:

```
count               1 byte: number of cases, 1..255
value_1, rel_1      2 words: case value, then offset to its code
value_2, rel_2
...
value_n, rel_n
<default code>      starts immediately after the table
```

* Entries **must be sorted in ascending signed order** of `value`. The 6502
  VM stops searching as soon as it passes `val`.
* The 6502 search decides "passed" from the sign of the 16-bit difference
  `val - value`, without overflow correction. So **the case values must span
  at most 32767** (largest minus smallest ≤ `$7FFF`). Otherwise a `val` that
  is in the table can be missed. Split wider tables into separate `SEL`s or
  compare-and-branch chains.
* Each `rel_i` is relative to the address of that `rel_i` word.
* If no value matches, execution continues right after the table, so the
  default (`OTHERWISE`) code goes there. If there is no default, put the
  code that follows the `WHEN` there.
* The PLASMA compiler places the table after the last case body (or just
  before the `OTHERWISE` body). The code before the table does `BRNCH`
  around it (see the example below). Case values that are addresses need
  relocation entries.

### Calls and frames

<a name="frames"></a>

| Op | Name | Operands | Size | Stack effect | Description |
|----|------|----------|------|--------------|-------------|
| $54 | CALL | addr | 3 | `( args… -- results… )` | Call the 6502-callable routine at `addr` with a 6502 `JSR`. Bytecode resumes after the operand when it returns. |
| $56 | ICAL | — | 1 | `( args… addr -- results… )` | Indirect call: pop `addr` (TOS, pushed after the arguments), then call as `CALL`. Used for function pointers. |
| $58 | ENTER | framesize, nparams | 3 | `( p1 … pn -- )` | Allocate a local frame of `framesize` bytes, then pop `nparams` words into it: parameter *k* (1-based, left to right) goes to offset `2*(k-1)`. So the first parameter is at offset 0 and the last, which was TOS, at `2*(n-1)`. `framesize` includes the parameters (`framesize ≥ 2*nparams`). Other locals are **not** initialized. |
| $5A | LEAVE | framesize | 2 | `( -- )` | Release the frame and return to the caller (6502 `RTS`). `framesize` must equal the one given to `ENTER`. |
| $5C | RET | — | 1 | `( -- )` | Return to the caller without touching the frame. Only for functions that have no `ENTER`. |
| $C0 | NATV | — | 1 | — | Leave the interpreter and jump to the native 6502 code that immediately follows this byte. No opcode returns to bytecode afterward; the native code normally ends the function with `RTS`. Not portable: see [Significant discrepancies](#significant-discrepancies). |

How `ENTER`/`LEAVE` manage the frame differs between VMs. The 64K Apple II
and Apple 1 VMs subtract `framesize` from IFP and `LEAVE` adds it back,
saving nothing. The 128K Apple II and Apple /// VMs push the old IFP on the
6502 stack and allocate the frame below PP; `LEAVE` pops it. The 65802 VM
stores the old IFP just past the frame, and the C VM stores it in the pool.
The rules a compiler must follow are therefore:

* Every function that executes `ENTER` must exit only through `LEAVE` with
  the **same** `framesize`, on every path. Never use `RET` after `ENTER`. On
  VMs that pushed IFP, `RET` would return to the wrong address.
* Don't rely on anything about memory outside the frame.

Calling convention used by the PLASMA compilers:

1. The caller pushes arguments **left to right**, then `CALL addr` (or pushes
   the function address and uses `ICAL`). The argument count must match the
   callee's `nparams` exactly; nothing checks it.
2. A function that has parameters or locals starts with
   `ENTER framesize, nparams` and returns with `LEAVE framesize`. A function
   with locals but no parameters uses `ENTER framesize, 0`.
3. A function with no parameters and no locals omits `ENTER` and returns
   with `RET`.
4. Return values are whatever the function leaves on the evaluation stack.
   A PLASMA function declared `#n` always returns exactly `n` values (the
   default is 1). The compiler pads a short `RETURN`, or falling off the end
   of the function, with `CN 0`. With several values, the first is deepest
   and the last is TOS. The caller must `DROP` any it doesn't use, for
   example a call used as a statement.
5. Before returning, everything else the function pushed must be popped,
   including the two values of any enclosing `FOR` loop, so that only the
   results remain.

#### How a bytecode function is entered

`CALL` always jumps to **6502 code**. A bytecode function is therefore
reached through a small 6502 stub that enters the interpreter. For modules,
**the loader builds these stubs**; the compiler never emits them.

* **Indirect entry** (modules): for each bytecode function listed in the
  RLD, the loader builds a 5-byte stub: `JSR` to the VM's indirect entry,
  then the word address of the bytecode. On the Apple II that is
  `$20 $D6 $03 lo hi` (`IINTRP` at `$03D6`); the C VM uses `$FF08`. Every
  relocated reference to the function is redirected to its stub. This is
  what lets the bytecode live in auxiliary or extended memory.
* **Direct entry** (only for non-module output, i.e. `xplasm` without `-M`):
  the function begins with `JSR INTERP` and its bytecode follows
  immediately. `INTERP` is `$03D0` on the Apple II; it is platform-specific.

Native 6502 functions (`asm` definitions) are called the same way. On the
6502 VMs, X indexes the evaluation stack on entry: `ESTKL,X` / `ESTKH,X` is
TOS, and the stack grows toward lower X (`DEX` to push, `INX` to pop). The
routine pops its arguments, pushes its results, must leave X consistent
with that, and ends with `RTS`. A and Y may be changed. The 65802 VM keeps
its own evaluation stack on the hardware stack, but it copies the values
into this zero-page layout around native calls, so native code sees the
same convention there.

## Significant discrepancies

### C VM vs. the 6502 VMs

The portable C VM (`src/vmsrc/plvm.c`, built as `src/plvm` and used to run
modules on a host machine) does not exactly match the 6502 VMs. These are
the differences found when the two were compared opcode by opcode:

| Opcode | 6502 VMs | C VM | How a compiler can avoid it |
|--------|----------|------|-----------------------------|
| `$80` NOT | `$FFFF` if TOS is 0, else 0 | `1` if TOS is 0, else 0 | Don't emit `NOT` when the value is kept. Emit `CN 0` + `ISEQ` (`$00 $40`) instead: `ISEQ` gives `$FFFF`/`0` on every VM. If you want C's 1/0 instead, `NOT` + `ANDI 1` (`$80 $3C $01`) gives 1/0 on every VM. Make sure no peephole optimizer turns that back into `NOT`. When the result is only used by a conditional branch, invert the branch instead (e.g. `BRTRU` in place of `NOT` + `BRFLS`). Branches on a `NOT` result are safe on both VMs, since they agree on which values are zero. |
| `$36` DIVMOD | `( a b -- a/b a%b )`: **remainder** is TOS | `( a b -- a%b a/b )`: **quotient** is TOS | Don't emit `DIVMOD`; use `DIV` and `MOD` separately. The current PLASMA compilers never emit it either. |
| `$C0` NATV | Jumps to native code after the opcode | Not implemented: reported as an illegal opcode, and the VM exits | Don't emit `NATV`. Put native code in a separate `asm` function and `CALL` it. (`NATV` also can't work where bytecode runs from auxiliary or extended memory.) |
| `$52` SEL | Table must be sorted ascending (signed); the search stops early, and the values must span ≤ 32767 | Linear search: unsorted or wide tables still work | Always sort, and keep the span ≤ 32767. Code that works on the C VM can fail on real hardware. |
| `$88` DIV, `$8A` MOD, `$36` DIVMOD, by zero | Garbage result, execution continues | Host process crashes (SIGFPE) | Check the divisor first if it might be zero. |

The `NOT` difference shows up with PLASMA's own compiler today. Its
optimizer compiles `x == 0` into `NOT`, so `puti(z == 0)` with `z` = 0
prints `-1` on an Apple II but `1` under `plvm` (checked with `plvm`).

Frame handling also differs internally between the VMs (see
[Frames](#frames)), but bytecode can't see the difference as long as it
follows the `ENTER`/`LEAVE` rules.

A compiler that avoids `NOT` for kept values, never emits `DIVMOD` or
`NATV`, keeps `SEL` tables sorted and within the 32767 span, and guards
against division by zero produces code that behaves the same on both.

### Cross-compiler optimizer: folded comparisons give 1, not `$FFFF`

This one is not a VM difference. It's in the PLASMA cross-compiler
(`xplasm`) itself, so it affects every VM.

At run time, PLASMA's comparisons and `not` produce `$FFFF` for true
(on the 6502 VMs; see `NOT` above for the C VM). But the cross-compiler's
optimizer (`-O`), when it folds a comparison or `not` whose operands are
all constants, produces **1**. This is in `crunch_seq()` in
`src/toolsrc/codegen.c`:
- `EQ`, `NE`, `GE`, `LT`, `GT` and `LE` fold to `? 1 : 0`;
- `not` folds to `? 0 : 1`.

So the same source gives different values depending on `-O`. Checked
with `xplasm` and `plvm`:

| Expression | `xplasm -AMS` | `xplasm -AMOS` |
|------------|---------------|----------------|
| `3 == 3` | -1 | **1** |
| `3 < 4` | -1 | **1** |
| `x == 3` with `x` = 3 (not constant, so not folded) | -1 | -1 |

On a 6502 VM, `not 0` likewise gives -1 without `-O` and 1 with it.
Under `plvm` both print 1, because of the C VM's own `NOT` difference.

The self-hosted compiler doesn't have this problem. Its optimizer,
`src/toolsrc/codeopt.pla`, folds with PLASMA's own operators and
`TRUE`, which give `$FFFF`. So a program can also behave differently
depending on **which** PLASMA compiler built it.

Any test of a folded comparison against `TRUE` or `-1` is affected, as
is any use of the value in arithmetic (e.g. `count = count - (a < b)`).
A plain `if`/`while` test is not, because 1 and `$FFFF` are both
non-zero.

**For compiler writers:** when folding constants, produce the same
value that the run-time opcode would. That means `$FFFF` for PLASMA
semantics, or 1 for C semantics if the compiler normalizes comparison
results itself. The fix for PLASMA would be to fold to `-1` in
`codegen.c`, matching `codeopt.pla`.

### Current opcodes vs. the 2018 README table

The opcode table that used to be in `README.md` (removed in commit
`ed280b35`, April 2018) uses an older numbering and **must not be used to
generate code**. It was already out of date when it was removed. The
numbering changed mostly in early March 2018:

* **Arithmetic moved from `$02`–`$1E` to `$82`–`$9E`** (commit `334bf1ec`,
  "Nybble constants encoded in opcode"). This freed `$00`–`$1E` for the
  one-byte constants `CN 0`–`CN 15`, saving a byte on the most common
  constants. So ADD went from `$02` to `$82`, NOT from `$20` to `$80`, IDXW
  from `$1E` to `$9E`, and so on.
* **LOR (`$22`) and LAND (`$24`) were replaced by the short-circuit branches**
  BRAND/BROR (`$AC`/`$AE`) (commit `eff01c5f`). BREQ and BRNE moved into
  their old slots from `$3C`/`$3E`.
* **Branches moved:** BRGT and BRLT moved from `$38`/`$3A` to `$A0`/`$A2`,
  next to the new fused `FOR` loop branches.
* **Dropped:** NOP (`$34`), IBRNCH (`$52`) and the separate IDXB entry.
  DUP moved from `$32` to `$34`. **Added:** MINUS1, DROP2, ADDI/SUBI/ANDI/ORI,
  SEL, INCBRLE/ADDBRLE/DECBRGE/SUBBRGE, the ADDxx/IDXxx memory-operand forms,
  NATV, JUMP and JUMPZ.
* **Kept their numbers:** LA, LLA, CB, CW, CS, DROP, DIVMOD, the ISxx
  comparisons, BRFLS/BRTRU, BRNCH, CALL/ICAL/ENTER/LEAVE/RET, CFFB, and all
  loads and stores (`$60`–`$7E`).

## Module (REL) file format

A compiler must also produce the relocatable module wrapper so the system can
load the code. This is the format written by `src/toolsrc/codegen.c` (with
`-M`) and read by `loadmod()` in `src/vmsrc/apple/cmd.pla` and `load_mod()`
in `src/vmsrc/cmdsys.c`. On ProDOS, modules are files of type `$FE` (REL),
aux type `$1000`. They are assembled as if loaded at `$1000`, with a 2-byte
length word in front at `$0FFE` (`acme --setpc 4094`). The loader relocates
them to wherever it puts them.

```
$0FFE  word   length of header + data + bytecode (everything before the RLD)
$1000  word   $6502                  magic number (_SEGBEGIN)
       word   system flags           (_SYSFLAGS; usually 0)
       word   address of first bytecode byte  (_SUBSEG)
       word   number of bytecode defs, including init  (_DEFCNT)
       word   address of init function, or 0  (_INIT)
       DCI…   names of modules this module depends on, then a $00 byte
       data + native code segment    (globals, asm functions)
       bytecode segment              (starts at _SUBSEG; all bytecode defs, init last)
       RLD                           (relocation dictionary; see below)
       ESD                           (external/entry symbol dictionary)
```

Layout rules:

* **Everything before `_SUBSEG` is data or native code; everything from
  `_SUBSEG` to the end of the length is bytecode.** The loader treats any
  relocated address at or above `_SUBSEG` as a bytecode function, and some
  VMs move the bytecode segment out of main memory.
* If no data comes before `_SUBSEG`, or the last data label marks an empty
  item, emit a **pad byte** first. Otherwise that label would have the same
  address as `_SUBSEG` and be mistaken for a function.
* **The init function must be the last bytecode in the segment.** After
  init runs, the loader frees memory from the start of init onward (unless
  init asks to keep it).
* The header and the dependency list must fit in the first 128 bytes of
  the file. The loader reads only that much to find dependencies, which
  leaves 116 bytes for the dependency names and their `$00` terminator.
* `_DEFCNT` must count every bytecode function, init included. The loader
  sizes its stub table from it.

A **DCI** string is ASCII with the high bit **set** on every character
except the last, e.g. `"PUTS"` → `$D0 $D5 $D4 $53`. Symbol and module names
are uppercase.

**Dependencies** are module names, such as `CMDSYS` (the resident system
module, which exports `puts`, `puti`, `heapalloc` and so on; see
`src/inc/cmdsys.plh`). The loader loads each one that is not already loaded
before relocating this module.

**RLD** entries are 4 bytes each, terminated by a `$00` byte:

| Byte 0 (flags) | Bytes 1–2 | Byte 3 | Meaning |
|----------------|-----------|--------|---------|
| `$02` | **absolute** address of a bytecode function, assembled at `$1000` (e.g. `$100C`) | 0 | Bytecode function entry. These must all come **first** in the RLD, one per function, **excluding** init. The loader builds an indirect-entry stub for each. |
| `$81` | offset of the word to patch, **relative to `$1000`** (`_SEGBEGIN`) | 0 | Internal word fixup: the loader adds the module's load offset to the word. If the result is at or above `_SUBSEG`, it is replaced by the address of that function's stub. |
| `$01` | offset of the byte to patch | 0 | Internal byte fixup of an address's **low** byte. |
| `$41` | offset of the byte to patch | low byte of the unrelocated address | Internal byte fixup of an address's **high** byte. The low byte is needed to compute the carry. |
| `$91` | offset of the word to patch | ESD index | External word fixup: the loader adds the imported symbol's address to the word, which is normally assembled as 0, or as an offset to add. Externals must be **word** fixups; the 6502 loader always patches a word for them. |

Flag bits: `$80` = word (otherwise byte), `$40` = high byte, `$10` = external,
`$01` = fixup (always set on fixups). `$02` marks a function entry.

Every operand that holds an address inside the module (`LA`, `LAB`, `LAW`,
`SAB`, `SAW`, `DAB`, `DAW`, `ADDAB`, `ADDAW`, `IDXAB`, `IDXAW`, `CALL`, `JUMP`,
`JUMPZ`, `SEL` case values that are addresses, and initialized pointer data)
needs an RLD entry. Branch offsets (`rel`) are position-independent and
don't. Fixed hardware addresses such as `$C000` must **not** get an entry.

The only bytecode addresses a module may reference are function entry
points. The 6502 loader replaces any other relocated address inside the
bytecode segment (say, the address of a `CS` string or a label in the
middle of a function) with **0**, with no error.

**ESD** entries, terminated by a `$00` byte:

* Import: DCI name, flag `$10`, word ESD index. The index is the number
  used in byte 3 of `$91` RLD entries; only its low byte is compared, so
  use 0–254. Indexes don't have to be dense.
* Export: DCI name, flag `$08`, word address of the symbol (assembled at
  `$1000`; the loader relocates it). For a bytecode function, give the
  address of its bytecode, and the loader substitutes the stub.

Imports are resolved against the global symbol table, which holds the
exports of CMDSYS and of every module loaded so far. A missing symbol makes
the loader print `?NAME` and fail the load.

**System flags** (`_SYSFLAGS`) are OR-ed into the system's flags when the
module loads. They mostly reserve memory such as the text and graphics
screen pages (`restxt1` = `$0001` … `vid80col` = `$0800`; the full list is in
`src/inc/cmdsys.plh`). Use 0 unless you need them.

**Init function:** for a program, init is the main program. If `_INIT` is
nonzero, the loader calls init once after relocation, and its return value
decides what happens next:

| Return value | Meaning |
|--------------|---------|
| negative | Load error (`-value` is the error code). |
| `0`–`$1FFF` | Normal. A program run from the command line is unloaded when init returns. |
| `$2000` (`modkeep`) or-ed in | Keep the module resident; libraries return this. Init's own code is still freed. |
| `$4000` (`modinitkeep`) or-ed in | Also keep init's code. |
| `$1000` (`modflagkeep`) or-ed in | Keep the system flags the module set after it is unloaded. |

The PLASMA compiler ends init with `CN 0` / `RET`.

## Examples

### A complete module: hello world

`src/samplesrc/hello.pla`:

```
include "inc/cmdsys.plh"
puts("Hello, world.\n")
done
```

compiled with `xplasm -AMO` and assembled with `acme --setpc 4094` gives this
54-byte file (PLASMA encodes `\n` as `$0D`, a carriage return):

| File offset | Address | Bytes | Meaning |
|-------------|---------|-------|---------|
| `00` | `$0FFE` | `27 00` | Length = `$0027` (header + data + bytecode). |
| `02` | `$1000` | `02 65` | Magic `$6502`. |
| `04` | `$1002` | `00 00` | System flags. |
| `06` | `$1004` | `12 10` | `_SUBSEG` = `$1012`. |
| `08` | `$1006` | `01 00` | `_DEFCNT` = 1 (just init). |
| `0A` | `$1008` | `12 10` | `_INIT` = `$1012`. |
| `0C` | `$100A` | `C3 CD C4 D3 D9 53` | Dependency `CMDSYS` (DCI). |
| `12` | `$1010` | `00` | End of dependencies. |
| `13` | `$1011` | `00` | Pad byte (no data before `_SUBSEG`). |
| `14` | `$1012` | `2E 0E 48 … 2E 0D` | `CS "Hello, world.\r"` (init starts here). |
| `24` | `$1022` | `54 00 00` | `CALL puts`, with the operand left 0 for an external fixup. |
| `27` | `$1025` | `00` | `CN 0`: init's return value. |
| `28` | `$1026` | `5C` | `RET`. |
| `29` | — | `91 23 00 04` | RLD: external word fixup at `$1000+$0023` (the `CALL` operand), ESD index 4. |
| `2D` | — | `00` | End of RLD. |
| `2E` | — | `D0 D5 D4 53 10 04 00` | ESD: import `PUTS`, flag `$10`, index 4. |
| `35` | — | `00` | End of ESD. |

There are no `$02` RLD entries because init is the only function, and init
is never listed.

### Functions, a FOR loop and a WHEN

This PLASMA source

```
byte total

def sumto(n)#1
  word i, sum
  sum = 0
  for i = 1 to n
    sum = sum + i
  next
  return sum
end

def classify(x)#1
  when x
    is 0
      return 'Z'
    is 1
      return 'O'
  wend
  return '?'
end

total = sumto(10)
done
```

compiles (`xplasm -AMO`) to the following bytecode. Offsets are relative to
the start of each function.

#### `sumto(n)`: locals and a FOR loop

Frame: `n` at offset 0, `i` at 2, `sum` at 4, so `framesize` = 6.

| Off | Bytes | Instruction | Stack after (TOS at right) | Notes |
|-----|-------|-------------|----------------------------|-------|
| 0  | `58 06 01` | ENTER 6,1 | `( )` | Pops `n` into L[0]. |
| 3  | `00` | CN 0 | `( 0 )` | |
| 4  | `76 04` | SLW [4] | `( )` | `sum = 0` |
| 6  | `66 00` | LLW [0] | `( n )` | Loop limit. |
| 8  | `02` | CN 1 | `( n 1 )` | Initial count. |
| 9  | `A0 0D 00` | BRGT +13 → 23 | `( n 1 )` | Skip loop if `1 > n`. Offset word at 10; 10 + 13 = 23. |
| 12 | `6E 02` | DLW [2] | `( n i )` | Loop top: `i = count`, keep count. |
| 14 | `66 04` | LLW [4] | `( n i sum )` | |
| 16 | `B2 02` | ADDLW [2] | `( n i sum+i )` | |
| 18 | `76 04` | SLW [4] | `( n i )` | `sum = sum + i` |
| 20 | `A4 F7 FF` | INCBRLE -9 → 12 | `( n i+1 )` | Offset word at 21; 21 - 9 = 12. |
| 23 | `76 02` | SLW [2] | `( n )` | Loop exit: store final count into `i`. |
| 25 | `30` | DROP | `( )` | Drop the limit. |
| 26 | `66 04` | LLW [4] | `( sum )` | Return value. |
| 28 | `5A 06` | LEAVE 6 | | Release frame and return; `sum` stays on the stack. |

#### `classify(x)`: SEL and its case table

| Off | Bytes | Instruction | Notes |
|-----|-------|-------------|-------|
| 0  | `58 02 01` | ENTER 2,1 | `x` → L[0] |
| 3  | `66 00` | LLW [0] | Push `x`. |
| 5  | `52 0D 00` | SEL +13 → 19 | Offset word at 6; the case table is at 6 + 13 = 19. |
| 8  | `2A 5A` | CB 'Z' | `is 0` body. |
| 10 | `5A 02` | LEAVE 2 | |
| 12 | `2A 4F` | CB 'O' | `is 1` body. |
| 14 | `5A 02` | LEAVE 2 | |
| 16 | `50 0B 00` | BRNCH +11 → 28 | Falling out of the last case jumps over the table (offset word at 17; 17 + 11 = 28). |
| 19 | `02` | case count = 2 | |
| 20 | `00 00 F2 FF` | value 0, rel -14 → 8 | Offset word at 22; 22 - 14 = 8. |
| 24 | `01 00 F2 FF` | value 1, rel -14 → 12 | Offset word at 26; 26 - 14 = 12. |
| 28 | `2A 3F` | CB '?' | Code after the table: reached on no match (there's no `OTHERWISE`) and by the `BRNCH` above. `return '?'` |
| 30 | `5A 02` | LEAVE 2 | |

#### Module init: calling a function

| Bytes | Instruction | Notes |
|-------|-------------|-------|
| `14` | CN 10 | Argument. |
| `54 0C 10` | CALL sumto | Operand `$100C` is `sumto`'s bytecode. Its `$81` RLD entry makes the loader point it at `sumto`'s stub. |
| `78 0B 10` | SAB total | Store the low byte of the result in the global `total` at `$100B` (also relocated). |
| `00` | CN 0 | Init's return value: 0 = success. |
| `5C` | RET | Init has no locals, so no `ENTER`/`LEAVE`. |

The RLD for this module is `02 0C 10 00`, `02 2A 10 00` (function entries for
`sumto` at `$100C` and `classify` at `$102A`), `81 4C 00 00`, `81 4F 00 00`
(the `CALL` and `SAB` operands at `$104C` and `$104F`), then `00`. The ESD is
just `00`, because nothing is imported or exported.

### Testing generated modules

The host C VM runs a module directly. Name the file in lowercase, without
an extension, and run it from its directory:

```
cd src && make plvm          # builds src/plvm
cd /path/to/module && /path/to/PLASMA/src/plvm hello
```

`plvm -t` traces each opcode with the evaluation stack, and `plvm -s`
single-steps. Remember the [discrepancies](#significant-discrepancies):
code that passes under `plvm` should also be checked on a 6502 VM (real
hardware or an emulator) before relying on it.

## Sources

This document was compiled from the following files in this repository.
Where they disagree, the 6502 VM (`plvm02.s`) was taken as authoritative.

* `src/vmsrc/apple/plvm02.s`: Apple II 64K VM. Opcode table (`OPTBL`), the
  6502 implementation of every opcode, the interpreter entry points
  `DINTRP`/`IINTRP` at `$03D0`/`$03D6`, and the zero-page dispatch loop.
* `src/vmsrc/apple/plvm01.s`, `plvmjit02.s`, `plvm802.s`, `plvm03.s`,
  `plvmjit03.s` and `src/vmsrc/c64/plvmc64.s`: the other VMs. Their opcode
  tables were checked to be identical, and their `ENTER`/`LEAVE`/`CS` code
  was compared.
* `src/vmsrc/apple/plvmzp.inc`: zero-page layout (evaluation stack size,
  IFP, PP).
* `src/vmsrc/plvm.c`, `plvm.h` and `cmdsys.c`: portable C VM and its
  module loader. Per-opcode comments, a readable reference implementation
  (`vm_interp`), and the entry-stub addresses.
* `src/toolsrc/codegen.c`: the cross-compiler's code generator. Exact
  encodings emitted for each opcode, constant selection, case tables, call/
  enter/leave usage, module header, pad byte, RLD and ESD output.
* `src/toolsrc/parse.c`: code shapes for `FOR`/`NEXT`, `WHEN`/`IS`
  (including the sorted case table) and `RETURN` (popping `FOR` values,
  padding return values).
* `src/vmsrc/apple/cmd.pla`: Apple II module loader (`loadmod`, `reloc`,
  `adddef`, `lookupdef`, `lookupidx`, `execmod`). REL format interpretation,
  def stubs, init return values.
* `src/inc/cmdsys.plh`: system flag and module-keep constants, and the
  CMDSYS exports.
* `doc/PLASMA.md`, "Implementation" section: overview of the VM's stacks
  and frames.
* The opcode table formerly in `README.md`, removed in commit `ed280b35`
  (2018), and commits `334bf1ec` (nybble constants) and `eff01c5f`
  (short-circuit AND/OR), which explain the renumbering.
* Verification: both examples were compiled with `src/xplasm -AMO`,
  assembled with `acme --setpc 4094`, and the resulting bytes and offsets
  checked against this document. Both modules were run under `src/plvm`,
  as was a test of `NOT` that confirmed the C VM's `1` result.
