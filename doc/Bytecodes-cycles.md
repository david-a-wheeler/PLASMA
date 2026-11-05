# VM cycle counts (6502 Apple II VM)

How the 6502 cycle count for each PLASMA VM operation was determined, for
`plvm02.s` (the 6502 Apple II VM, `src/vmsrc/apple/`). This is the reference
for the "Cycles" column in `Bytecodes.md` and for peephole cost analysis:
when comparing code, the **total** per-op cycle count is what matters.

This file is written operation-by-operation so a reviewer can re-verify each
number: check each 6502 instruction's cycle count, add them up, and confirm
the op-specific subtotal, the ending dispatch, and the total.

> **Status: draft, pending re-review.** These are a first careful pass, not
> yet final. Re-verify the numbers against `plvm02.s` (and the 6502 timing
> table) before they are considered settled and copied into the "Cycles"
> column of `Bytecodes.md`. The per-op breakdowns are meant to make that
> re-review tractable.
>
> One thing to re-check in particular: upstream has changed the boolean
> "true" value so it is consistently `$FFFF` (it used to be `1` in a few
> places). Any op whose cost depends on that value (the `ISTRU`/`ISFLS`
> result-push, the `NOT` handler, etc.) should be re-verified once that
> change is merged into this branch.

## The counting convention

The interpreter is a fetch/dispatch loop. Every op's handler does its work and
then **fetches and dispatches the next op**. So the "cycle count of an op" is
the cost of running its handler including that trailing fetch/dispatch, and the
sum of a sequence's per-op counts is (approximately) the cost of the sequence.

There are three ways a handler ends, and the trailing dispatch differs:

| Ending | Meaning | Trailing cost |
|--------|---------|---------------|
| `JMP NEXTOP` | one-operand op (touches only TOS) | 6 + 16 = **22** |
| `JMP DROP`   | two-operand op (combines TOS and NOS, drops TOS) | 6 + 18 = **24** |
| (inline)     | `DROP` itself: inlines the fetch/dispatch | **18** |

So the **total** for an op = its op-specific work + the trailing cost above.

`NEXTOP` (at `$F0`) is the fetch/dispatch shared by every op:

```
INY                     ; 2   advance the IP offset
LDA     $FFFF,Y         ; 5   fetch the next opcode (abs,Y; +1 on page cross)
STA     OPIDX           ; 3   zero page
JMP     (OPTBL)         ; 6   indirect via zero page
```

= 2 + 5 + 3 + 6 = **16** (base; the `LDA $FFFF,Y` is +1 when the IP crosses a
256-byte page boundary). `DROP` is `INX` (2) then the same 16 = **18**.

## 6502 timing used (cycles, base / no page cross)

The VM's scratch memory is all zero page (`plvmzp.inc`): the stack
`ESTKH=$A0`/`ESTKL=$C0` (indexed by X), the frame `IFP=$E0`, `SRC=$06`,
`TMP=$E7`, `IP=$F2`, `IPY=$E4`, `OPIDX=$F7`, `DVSIGN=$EA`.

Only the instruction forms the VM actually uses are listed.

| Instruction | Cycles |
|-------------|--------|
| `LDA`/`AND`/`ORA`/`EOR`/`ADC`/`SBC`/`CMP` `#imm` | 2 |
| `LDA`/`STA`/`AND`/`ORA`/`EOR`/`ADC`/`SBC`/`CMP` `zp` | 3 |
| `LDA`/`STA`/`AND`/`ORA`/`EOR`/`ADC`/`SBC`/`CMP` `zp,X` | 4 |
| `LDA`/`STA`/`AND`/`ORA`/`EOR`/`ADC`/`SBC`/`CMP` `abs` | 4 |
| `LDA`/`STA` `(zp,X)` | 5 |
| `LDA` `(zp),Y` or `abs,Y` | 5 (6 on a page cross) |
| `ASL`/`ROL`/`LSR` `A` | 2 |
| `INC`/`DEC`/`ASL`/`ROL`/`LSR` `zp` | 5 |
| `INC`/`DEC`/`ASL`/`ROL`/`LSR` `zp,X` | 5 |
| `TAX`/`TXA`/`TAY`/`TYA` | 2 |
| `INX`/`DEX`/`INY`/`DEY` | 2 |
| `CLC`/`SEC` | 2 |
| `PHA` | 3 |
| `PLA` | 4 |
| `JMP abs` | 3 |
| `JMP (zp)` | 6 |
| `JSR` / `RTS` | 6 |
| `Bcc` not taken | 2 |
| `Bcc` taken | 3 |

The addresses that matter, and the form they take:

| Operand | Address | Form | Cycles |
|---------|---------|------|--------|
| stack low/high `ESTKL,X`/`ESTKH,X` | `$C0,X`/`$A0,X` | `zp,X` | 4 |
| stack indirect `(ESTKH-1,X)` | `$9F,X` | `(zp,X)` | 5 |
| frame `(IFP),Y` | `($E0),Y` | `(zp),Y` | 5 |
| frame `IFPL`/`IFPH` | `$E0`/`$E1` | `zp` | 3 |
| operand `(IP),Y` | `($F2),Y` | `(zp),Y` | 5 |
| opcode fetch `$FFFF,Y` | `$FFFF,Y` | `abs,Y` | 5 (+1 cross) |
| temps `TMPL`/`TMPH`/`IPY`/`OPIDX`/`DVSIGN` | zero page | `zp` | 3 |
| `SRC`/`SRCL`/`SRCH` | `$06`/`$06`/`$07` | `zp` | 3 |
| `IPL`/`IPH` | `$F2`/`$F3` | `zp` | 3 |

The two most common corrections versus a naive count:
1. `ESTKL,X`/`ESTKH,X` are **`zp,X` (4 cycles)**, not absolute,X (5).
2. Frame reads are **`(IFP),Y` (5 cycles)**, and the operand/opcode fetches are
   5-cycle indirects that cost 6 when they cross a 256-byte page boundary.

## Constants

### ZERO — `$00` (34)

The constant 0. `$00` is the bit form of `CN 0`, but it dispatches to the
`ZERO` handler, not `CN`. Because A already holds the opcode (0) on entry,
`ZERO` stores A directly to both bytes instead of the `LSR` + `LDA #$00` that
`CN` does. ZERO is therefore 4 cycles faster than CN 1-15, and is treated as a
distinct operand for cost purposes.

```
DEX                     ; 2
STA     ESTKL,X         ; 4   (A is 0)
STA     ESTKH,X         ; 4   (A is 0)
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 10 + 22 = **34**.

### CN 1-15 — `$02`-`$1E`, handler `CN` (38)

```
DEX                     ; 2
LSR                     ; 2   A = opcode/2 = n
STA     ESTKL,X         ; 4
LDA     #$00            ; 2   high byte 0
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 14 + 22 = **38**.

### MINUS1 — `$20` (36)

```
DEX                     ; 2
LDA     #$FF            ; 2
STA     ESTKL,X         ; 4
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 12 + 22 = **36**.

### CB — `$2A` (43)

```
DEX                     ; 2
LDA     #$00            ; 2
STA     ESTKH,X         ; 4
INY                     ; 2   read the operand byte
LDA     (IP),Y          ; 5
STA     ESTKL,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 19 + 22 = **43**.

### CFFB — `$5E` (43)

Same shape as CB, but the high byte is `$FF` instead of `$00`.

```
DEX                     ; 2
LDA     #$FF            ; 2
STA     ESTKH,X         ; 4
INY                     ; 2
LDA     (IP),Y          ; 5
STA     ESTKL,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 19 + 22 = **43**.

### CW — `$2C` (46)

```
DEX                     ; 2
INY                     ; 2
LDA     (IP),Y          ; 5   low byte
STA     ESTKL,X         ; 4
INY                     ; 2
LDA     (IP),Y          ; 5   high byte
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 24 + 22 = **46**.

### LA — `$26` (48)

Same as CW (reads a 2-byte address operand, pushes it), but with a `BMI -`
loop back to the shared IP-renormalize prologue (a rare IP-wrap; not taken
normally), so it is 2 cycles more than CW. `LA` exists so a relocated address
is clear in listings.

```
INY                     ; 2
BMI     -               ; 2   (not taken normally)
DEX                     ; 2
LDA     (IP),Y          ; 5   addr low
STA     ESTKL,X         ; 4
INY                     ; 2
LDA     (IP),Y          ; 5   addr high
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 26 + 22 = **48**.

### LLA — `$28` (51)

Pushes `IFP + b` (the address of a local): reads the 1-byte offset, adds it to
the frame pointer. The `BMI -` is a rare IP-wrap (it loops back to the shared
`-` renormalize prologue only when Y is high); it is not taken normally, so the
prologue is not in the per-op count (it is a ~every-256-bytes overhead; see
Notes).

```
INY                     ; 2
BMI     -               ; 2   (not taken normally)
LDA     (IP),Y          ; 5   offset
DEX                     ; 2
CLC                     ; 2
ADC     IFPL            ; 3
STA     ESTKL,X         ; 4
LDA     #$00            ; 2
ADC     IFPH            ; 3
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 29 + 22 = **51**.

### CS — `$2E` (59)

In-line string literal. Pushes the address of the length-prefixed string and
skips past it (the final `TAY` loads the length byte into Y, so the next fetch
advances over the characters). The cycle count is the address push; it does not
grow with the string length. (VMs that run bytecode from outside main memory
also copy the string into the pool — an extra cost, not counted here.)

```
DEX                     ; 2
TYA                     ; 2
SEC                     ; 2
ADC     IPL             ; 3
STA     IPL             ; 3
STA     ESTKL,X         ; 4
LDA     #$00            ; 2
TAY                     ; 2
ADC     IPH             ; 3
STA     IPH             ; 3
STA     ESTKH,X         ; 4
LDA     (IP),Y          ; 5   length byte
TAY                     ; 2   skip the string on the next fetch
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 37 + 22 = **59**.

## Stack manipulation

### DROP — `$30` (18)

```
INX                     ; 2
; (inline fetch/dispatch) = NEXTOP
INY                     ; 2
LDA     $FFFF,Y         ; 5
STA     OPIDX           ; 3
JMP     (OPTBL)         ; 6
```
= **18** (no separate `JMP`; the fetch/dispatch is inlined).

### DROP2 — `$32` (26)

```
INX                     ; 2   drop one
JMP     DROP            ; 6   (INX + fetch/dispatch)
; DROP                  ; 18
```
op-specific 2 + 24 = **26**.

### DUP — `$34` (44)

```
DEX                     ; 2
LDA     ESTKL+1,X       ; 4   old TOS is now NOS
STA     ESTKL,X         ; 4
LDA     ESTKH+1,X       ; 4
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 16 + 22 = **44**.

## Arithmetic and logic

The two-operand ops (`ADD`, `SUB`, `AND`, `IOR`, `XOR`, `IDXW`) combine TOS
and NOS and drop TOS, so they end with `JMP DROP` (24). The rest touch only
TOS and end with `JMP NEXTOP` (22).

### ADD — `$82` (50)

```
LDA     ESTKL,X         ; 4
CLC                     ; 2
ADC     ESTKL+1,X       ; 4
STA     ESTKL+1,X       ; 4
LDA     ESTKH,X         ; 4
ADC     ESTKH+1,X       ; 4
STA     ESTKH+1,X       ; 4
JMP     DROP            ; 6
; DROP                  ; 18
```
op-specific 26 + 24 = **50**.

### SUB — `$84` (50)

Same shape as ADD but `SEC`/`SBC` and the operands swapped (TOS − NOS):

```
LDA     ESTKL,X         ; 4
SEC                     ; 2
SBC     ESTKL+1,X       ; 4
STA     ESTKL+1,X       ; 4
LDA     ESTKH,X         ; 4
SBC     ESTKH+1,X       ; 4
STA     ESTKH+1,X       ; 4
JMP     DROP            ; 6
; DROP                  ; 18
```
op-specific 26 + 24 = **50**.

### MUL — `$86` (variable, ~580*)

Shift-and-add multiply. The cost is dominated by two loops over the
**multiplier's** bits. The VM first swaps the two values so the *smaller* is the
multiplier (the loop driver), so a small operand is much cheaper. The estimate
below is for a **full 16×16 multiply with half the multiplier bits set** (the
"average" assumption: a random 16-bit multiplier has ~half its bits set, and
its high byte non-zero, so both loops run).

```
MUL:    STY     IPY             ; 3   setup
        LDA     ESTKH,X         ; 4
        CMP     ESTKH+1,X       ; 4
        BCS     +               ; 2 not-taken / 3 taken (+30 to swap)
        ...                     ; (swap so the smaller is the multiplier)
+       LDA     ESTKL+1,X       ; 4
        EOR     #$FF            ; 2
        STA     TMPL            ; 3
        LDA     #$00            ; 2
        STA     ESTKL+1,X       ; 4
        LDY     #$08            ; 2
;  -- _MULLPL: 8 iterations over the low byte of the multiplier --
_MULLPL LSR     TMPL            ; 5
        BCS     +               ; 2 (bit 1) / 3 (bit 0)
        STA     TMPH            ; 3   (bit 0 only: PRODH = 0)
        LDA     ESTKL+1,X       ; 4   PRODL + multL
        ADC     ESTKL,X         ; 4
        STA     ESTKL+1,X       ; 4
        LDA     TMPH            ; 3
        ADC     ESTKH,X         ; 4
+       ASL     ESTKL,X         ; 5   shift the multiplicand left
        ROL     ESTKH,X         ; 5
        DEY                     ; 2
        BNE     _MULLPL         ; 2
        BEQ     +               ; 3   -> _MULLPH
;  -- _MULLPH: up to 8 iterations over the high byte of the multiplier --
_MULLPH CLC                     ; 2
        TAY                     ; 2
        LDA     ESTKL+1,X       ; 4   PRODL + multL
        ADC     ESTKL,X         ; 4
        STA     ESTKL+1,X       ; 4
        TYA                     ; 2
        ADC     ESTKH,X         ; 4
-       ASL     ESTKL,X         ; 5
        ROL     ESTKH,X         ; 5
+       LSR     ESTKH+1,X       ; 5   shift out the next high multiplier bit
        BCS     _MULLPH         ; 2 (bit 1) / 3 (bit 0)
        BNE     -               ; 2 (more bits) / 3 (last)
        STA     ESTKH+1,X       ; 4
        LDY     IPY             ; 3   ending
        JMP     DROP            ; 6
; DROP                  ; 18
```

Justification of the **~580** average:
- **Setup** (no swap): 3+4+4+2+17 = **30** (swap adds ~30, ~50% of the time).
- **`_MULLPL`, 8 iterations.** A bit-1 iteration is 5+2+5+5+2+2 = 21; a bit-0
  iteration adds the `STA`+add (3+4+4+4+3+4 = 22), for 46. With half the bits
  set, the average is (21+46)/2 = **33.5**, so 8 × 33.5 = **268**.
- **`_MULLPH`, ~8 iterations** (a full high byte, half its bits set). Per bit:
  the add (~22) when the bit is set, the multiplicand shift (10) always, and the
  bit check (LSR 5 + BCS ~2.5 + BNE ~2.5 = 10); average ~31, so 8 × 31 = **248**.
- **Ending**: BEQ 3 + LDY 3 + JMP DROP 6 + DROP 18 = **30**.

Total 30 + 268 + 248 + 30 = **576 ≈ 580**. If the multiplier's high byte is 0
(one-byte multiplier, ~small operand), `_MULLPH` runs 0 times and the total is
~**330**. *Varies with the multiplier's bit pattern and the swap.

### DIV / MOD / DIVMOD — `$88`/`$8A`/`$36` (variable, ~700*)

All three call the shared restoring-division routine `_DIV`. Its shape:

```
_DIV:   STY     IPY             ; 3   setup (~30 total)
        LDY     #$11            ; 2
        ... (remainder/sign setup) ...
_DIV1   ASL     ESTKL+1,X       ; 5   shift the dividend left
        ROL     ESTKH+1,X       ; 5   until it is >= the divisor
        DEY                     ; 2
        BCC     _DIV1           ; 2/3 ; (up to ~16 shifts, ~12/iter => ~190)
_DIVLP  ROL     TMPL            ; 5   the 16-step restoring division
        ROL     TMPH            ; 5
        LDA     TMPL            ; 3
        CMP     ESTKL,X         ; 4
        LDA     TMPH            ; 3
        SBC     ESTKH,X         ; 4
        BCC     +               ; 2/3 ; (skip the subtract when remainder<dvsr)
        STA     TMPH            ; 3   (~30/iter x 16 => ~480)
        LDA     TMPL            ; 3
        SBC     ESTKL,X         ; 4
        STA     TMPL            ; 3
        SEC                     ; 2
+       ROL     ESTKL+1,X       ; 5
        ROL     ESTKH+1,X       ; 5
        DEY                     ; 2
        BNE     _DIVLP          ; 2/3
_DIVEX  INX                     ; 2   ending (~15)
        LDY     IPY             ; 3
        RTS                     ; 6
```

Justification of the **~700** average: setup ~30 + the `_DIV1` alignment shift
(~16 iterations × ~12 = ~190) + the 16-step restoring division (~16 × ~30 =
~480) + ending ~15 = **~715 ≈ 700**. `DIV` returns the quotient, `MOD` loads
the remainder, `DIVMOD` leaves both; all three pay the same `_DIV` cost. A
short dividend (fewer leading shifts) is cheaper. *Varies with the operands.

### INCR — `$8C` (29 / 35)

Two cases (does the low byte wrap to 0?):

```
INC     ESTKL,X         ; 5
BEQ     +               ; 2 not-taken / 3 taken
JMP     NEXTOP          ; 6      (no wrap: 5+2+6+16 = 29)
; NEXTOP                ; 16
+   INC     ESTKH,X     ; 5      (wrap: 5+3+5+6+16 = 35)
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```

### DECR — `$8E` (33 / 39)

Two cases (was the low byte 0, needing a borrow from the high byte?):

```
LDA     ESTKL,X         ; 4
BEQ     +               ; 2 not-taken / 3 taken
DEC     ESTKL,X         ; 5
JMP     NEXTOP          ; 6      (no borrow: 4+2+5+6+16 = 33)
; NEXTOP                ; 16
+   DEC     ESTKL,X     ; 5      (borrow: 4+3+5+5+6+16 = 39)
DEC     ESTKH,X         ; 5
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```

### NEG — `$90` (44)

```
LDA     #$00            ; 2
SEC                     ; 2
SBC     ESTKL,X         ; 4
STA     ESTKL,X         ; 4
LDA     #$00            ; 2
SBC     ESTKH,X         ; 4
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 22 + 22 = **44**.

### COMP — `$92` (42)

```
LDA     #$FF            ; 2
EOR     ESTKL,X         ; 4
STA     ESTKL,X         ; 4
LDA     #$FF            ; 2
EOR     ESTKH,X         ; 4
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 20 + 22 = **42**.

### AND / IOR / XOR — `$94`/`$96`/`$98` (48 each)

```
LDA     ESTKL+1,X       ; 4
AND     ESTKL,X         ; 4      (IOR: ORA; XOR: EOR)
STA     ESTKL+1,X       ; 4
LDA     ESTKH+1,X       ; 4
AND     ESTKH,X         ; 4
STA     ESTKH+1,X       ; 4
JMP     DROP            ; 6
; DROP                  ; 18
```
op-specific 24 + 24 = **48**.

### SHL / SHR — `$9A`/`$9C` (variable, ~43 + 11×n*)

Per-bit shift loops (the "shifts are expensive" note). `SHL` shifts NOS (the
value) left by TOS (the count); the loop runs once per shift bit. `SHR` is the
mirror (a `ROR` loop with an extra `CMP #$80` to catch the sign bit, ~4 more
per bit).

```
SHL:  STY     IPY             ; 3
      LDA     ESTKL,X         ; 4   count
      CMP     #$08            ; 2
      BCC     +               ; 2 not-taken / 3 taken (count >= 8)
      ; (count >= 8: split the count into high/low, ~14 cycles)
+     TAY                     ; 2
      BEQ     +               ; 2 not-taken / 3 taken (count 0)
      LDA     ESTKL+1,X       ; 4   value low
-     ASL                     ; 2   shift left
      ROL     ESTKH+1,X       ; 5
      DEY                     ; 2
      BNE     -               ; 2   (loop, once per bit)
      STA     ESTKL+1,X       ; 4
+     LDY     IPY             ; 3
      JMP     DROP            ; 6
; DROP                ; 18
```
Count 0 (BEQ taken): 3+4+2+2+2+3+3+6+18 = **43**.
Count `n` (1-7): 3+4+2+2+2+2+4 + 11n + 4+3+6+18 = **50 + 11n**.
Count >= 8: adds ~14 to split the count (so ~64 + 11×(n-8)).
*Varies with the count; `SHR` adds ~4 per bit for the sign check.

### IDXW — `$9E` (57)

```
LDA     ESTKL,X         ; 4
ASL                     ; 2
ROL     ESTKH,X         ; 5
CLC                     ; 2
ADC     ESTKL+1,X       ; 4
STA     ESTKL+1,X       ; 4
LDA     ESTKH,X         ; 4
ADC     ESTKH+1,X       ; 4
STA     ESTKH+1,X       ; 4
JMP     DROP            ; 6
; DROP                  ; 18
```
op-specific 33 + 24 = **57**.

### NOT — `$80` (42 / 45)

Two cases. The `$FFFF` case (value is 0) is done by falling into `MINUS1`.

```
LDA     ESTKL,X         ; 4
ORA     ESTKH,X         ; 4
BEQ     +               ; 2 not-taken / 3 taken
LDA     #$00            ; 2      (nonzero: 4+4+2+2+4+4+6+16 = 42)
STA     ESTKL,X         ; 4
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
+   (MINUS1)            ; 12     (zero: 4+4+3+12+6+16 = 45)
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```

### ADDI — `$38` (41 / 47) and SUBI — `$3A` (41 / 47)

Two cases (carry/borrow out of the low byte):

```
INY                     ; 2
LDA     (IP),Y          ; 5      (SUBI: LDA ESTKL,X ; SEC ; SBC (IP),Y)
CLC                     ; 2
ADC     ESTKL,X         ; 4
STA     ESTKL,X         ; 4
BCC     +               ; 2 not-taken / 3 taken
INC     ESTKH,X         ; 5      (SUBI: DEC)
+   JMP     NEXTOP      ; 6
; NEXTOP                ; 16     (no carry: 2+5+2+4+4+2+6+16 = 41;
                              (carry:   2+5+2+4+4+3+5+6+16 = 47)
```

### ANDI — `$3C` (43)

```
INY                     ; 2
LDA     (IP),Y          ; 5
AND     ESTKL,X         ; 4
STA     ESTKL,X         ; 4
LDA     #$00            ; 2
STA     ESTKH,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 21 + 22 = **43**.

### ORI — `$3E` (37)

```
INY                     ; 2
LDA     (IP),Y          ; 5
ORA     ESTKL,X         ; 4
STA     ESTKL,X         ; 4
JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
op-specific 15 + 22 = **37**.

## Comparisons

All are signed, pop two values, and push `$FFFF` (true) or `0` (false). Each is
a compare (low byte, then high byte) that falls into `ISTRU` (push `$FFFF`) or
`ISFLS` (push `0`); both end with `JMP DROP`. The result push plus drop is a
fixed 34 (10 for `ISTRU`/`ISFLS`, 24 for the `JMP DROP`+`DROP`). The cost
depends on where the decision is made.

### ISEQ — `$40` and ISNE — `$42`

Compare the low bytes; if they differ the result is decided there (one branch
taken), otherwise compare the high bytes.

```
LDA     ESTKL,X         ; 4
CMP     ESTKL+1,X       ; 4
BNE     ISFLS/ISTRU     ; 2 not-taken / 3 taken
LDA     ESTKH,X         ; 4      (only if the low bytes matched)
CMP     ESTKH+1,X       ; 4
BNE     ISFLS/ISTRU     ; 2 not-taken / 3 taken
; (ISTRU or ISFLS)      ; 10
JMP     DROP            ; 6
; DROP                  ; 18
```
- low byte decides: 4+4+3+10+6+18 = **45**.
- high byte decides (low matched): 4+4+2+4+4+3+10+6+18 = **55**.
- equal (no branch taken, ISEQ) / all-differ (ISNE): 4+4+2+4+4+2+10+6+18 = **54**.

ISEQ is `true=54 / false=45 or 55`; ISNE is the mirror, `true=45 or 55 /
false=54`. For a typical compare use **~50**.

### ISGT / ISLT / ISGE / ISLE — `$44`/`$46`/`$48`/`$4A` (~54)

Signed compare with overflow (sign-change) handling. `ISGE` (the others are the
same shape):

```
ISGE:  LDA     ESTKL+1,X     ; 4   TOS low
       CMP     ESTKL,X       ; 4
       LDA     ESTKH+1,X     ; 4
       SBC     ESTKH,X       ; 4   (TOS - NOS, signed)
       BVS     +             ; 2 not-taken / 3 taken
       BPL     ISTRU         ; 2/3 ; (TOS - NOS >= 0 -> true)
       BMI     ISFLS         ; 2/3 ; (-> false)
+     BPL     ISFLS         ; 2/3 ; (overflow: the result is reversed)
       BMI     ISTRU         ; 2/3
; ISTRU/ISFLS: LDA #$FF/$00 + 2 STA = 10, then JMP DROP 6 + DROP 18 = 34
```
No overflow, true (BPL taken): 4+4+4+4+2+3+34 = **55**.
No overflow, false (BPL not, BMI taken): 4+4+4+4+2+2+3+34 = **53**.
Overflow (BVS taken): adds ~3.

Use **~54** (53 false / 55 true, no overflow) for planning.

## Loads

The absolute forms (`LAB`, `LAW`) read a 2-byte address operand (they do not
include the shared `-` IP prologue, which is a rare IP-wrap). The local forms
(`LLB`, `LLW`) read a 1-byte frame offset. `LB`/`LW` take the address on TOS.

### LB — `$60` (45) and LW — `$62` (55 / 61)

```
LB:  LDA     ESTKL,X       ; 4   addr = TOS
     STA     ESTKH-1,X     ; 4
     LDA     (ESTKH-1,X)   ; 5   byte at addr
     STA     ESTKL,X       ; 4
     LDA     #$00          ; 2
     STA     ESTKH,X       ; 4
     JMP     NEXTOP        ; 6  (LB: 23+22 = 45)
; NEXTOP              ; 16
```
`LW` is the same shape but loads a second byte at addr+1, with an `INC`/`BEQ`
page-change carry when the address ends in `$FF`:

```
LW:  LDA     ESTKL,X       ; 4   addr = TOS
     STA     ESTKH-1,X     ; 4
     LDA     (ESTKH-1,X)   ; 5   low byte
     STA     ESTKL,X       ; 4
     INC     ESTKH-1,X     ; 5   advance the address
     BEQ     +             ; 2/3 ; (to the page-change path)
     LDA     (ESTKH-1,X)   ; 5   high byte
     STA     ESTKH,X       ; 4
     JMP     NEXTOP        ; 6  (no page change: 33+22 = 55)
; NEXTOP              ; 16
+    INC     ESTKH,X     ; 5   (page change: carry into the high byte)
     LDA     (ESTKH-1,X)   ; 5
     STA     ESTKH,X       ; 4
     JMP     NEXTOP        ; 6  (page change: 39+22 = 61)
```
**55** (no page change) / **61** (address ends in `$FF`).

### LLB — `$64` (54) and LLW — `$66` (59)

```
LLB: INY                     ; 2
     LDA     (IP),Y          ; 5   offset
     STY     IPY             ; 3
     TAY                     ; 2
     DEX                     ; 2
     LDA     (IFP),Y         ; 5   byte at IFP+offset
     STA     ESTKL,X         ; 4
     LDA     #$00            ; 2
     STA     ESTKH,X         ; 4
     LDY     IPY             ; 3
     JMP     NEXTOP          ; 6  (LLB: 32+22 = 54)
; NEXTOP                ; 16
```
`LLW` (word) is the same with a second `(IFP),Y` read for the high byte (the
offset is always a valid in-frame index, so there is no negative/page-change
path): 2+5+3+2+2+5+4+2+5+4+3+22 = **59**.

### LAB — `$68` (61) and LAW — `$6A` (72)

Read a 2-byte absolute address operand, then load from it.

```
LAB: INY                     ; 2
     LDA     (IP),Y          ; 5   addr low
     STA     ESTKH-2,X       ; 4
     INY                     ; 2
     LDA     (IP),Y          ; 5   addr high
     STA     ESTKH-1,X       ; 4
     LDA     (ESTKH-2,X)     ; 5   the byte
     DEX                     ; 2
     STA     ESTKL,X         ; 4
     LDA     #$00            ; 2
     STA     ESTKH,X         ; 4
     JMP     NEXTOP          ; 6  (39+22 = 61)
; NEXTOP              ; 16
```
`LAB` = **61**.

`LAW` (word) stages the 2-byte address in `TMP` and reads the low and high
bytes at `(TMP),0` / `(TMP),1` (a plain indexed load, no page carry):
2+5+3+2+5+3+3+2+5+2+4+2+5+4+3+22 = **72**.

## Stores

The "S" forms pop the value and end with `JMP DROP` (24). The "D" (duplicate)
forms keep the value and end with `JMP NEXTOP` (22).

### SB — `$70` (44) and SW — `$72` (61 / 67)

```
SB:  LDA     ESTKL,X       ; 4   val low
     STA     ESTKH-1,X     ; 4   (stage the address)
     LDA     ESTKL+1,X     ; 4   addr low
     STA     (ESTKH-1,X)   ; 6   store the byte (abs,X indirect)
     INX                   ; 2
     JMP     DROP          ; 6   (SB: 20+24 = 44)
; DROP                ; 18
```
`SB` = **44**.

`SW` (word) stores both bytes, advances the address, and has a page-change
carry when the address low byte is `$FF`:

```
SW:  LDA     ESTKL,X       ; 4
     STA     ESTKH-1,X     ; 4
     LDA     ESTKL+1,X     ; 4
     STA     (ESTKH-1,X)   ; 6   low byte
     LDA     ESTKH+1,X     ; 4
     INC     ESTKH-1,X     ; 5   advance the address
     BEQ     +             ; 2/3 ; (page change)
     STA     (ESTKH-1,X)   ; 6   high byte
     INX                   ; 2
     JMP     DROP          ; 6
; DROP                ; 18
```
No page change (37+24): **61**. Page change (extra `INC ESTKH`, 43+24): **67**.

### SLB — `$74` (50) and SLW — `$76` (61)

Store to a frame offset, then drop the value (`JMP DROP`). The `BMI FIXDROP`
is a rare fixup (it is not taken for an already-relocated offset), so it is
not in the normal count.

```
SLB: INY                     ; 2
     LDA     (IP),Y          ; 5   offset
     STY     IPY             ; 3
     TAY                     ; 2
     LDA     ESTKL,X         ; 4   value low
     STA     (IFP),Y         ; 5   store at IFP+offset
     LDY     IPY             ; 3
     BMI     FIXDROP         ; 2   (not taken normally)
     JMP     DROP            ; 6   (SLB: 26+24 = 50)
; DROP                  ; 18
```
`SLW` adds a second `INY`/`LDA`/`STA` for the high byte: **61**.

### SAB — `$78` (58) and SAW — `$7A` (74)

Store to a 2-byte absolute address operand. `SAB` (byte) has the `BMI -`
IP-wrap check (not taken): INY 2 + BMI 2 + 2×(LDA 5 + STA 4) + LDA 4 + STA 6 +
24 = **58**. `SAW` (word) stages the address in `TMP` and stores both bytes at
`(TMP),0/1`, ending in `JMP DROP`: 2+5+3+2+5+3+3+2+4+5+2+4+5+3+2+24 = **74**.

### DLB — `$6C` (52) and DLW — `$6E` (57)

Store and keep the value (`JMP NEXTOP`). `DLB` is **52**; `DLW` (two stores) is
**57**.

### DAB — `$7C` (60) and DAW — `$7E` (70)

Store to a 2-byte absolute address and keep the value. `DAB` =
2+5+4+2+5+4+4+6+2+4+22 = **60**; `DAW` = 2+5+3+2+5+3+3+2+4+5+2+4+5+3+22 = **70**.

## Fused add/index with memory operand

These load a byte/word from a local or absolute address and add it (or twice
it, for the `IDX` forms) to TOS. The `BCC`/overflow branches are two cases.

### ADDLB — `$B0` (54 / 60) and ADDLW — `$B2` (67)

```
ADDLB: INY                     ; 2
       LDA     (IP),Y          ; 5   offset
       STY     IPY             ; 3
       TAY                     ; 2
       LDA     (IFP),Y         ; 5   local byte
       CLC                     ; 2
       ADC     ESTKL,X         ; 4
       STA     ESTKL,X         ; 4
       BCC     +               ; 2 not-taken / 3 taken
       INC     ESTKH,X         ; 5
+     LDY     IPY             ; 3
       JMP     NEXTOP          ; 6  (ADDLB: 54 no-carry / 60 carry)
; NEXTOP                ; 16
```
`ADDLW` (word) is **67**.

### ADDAB — `$B4` (61 / 67) and ADDAW — `$B6` (80)

Same as the local forms but reading a 2-byte absolute address. `ADDAB` is
**61** (no carry) / **67** (carry); `ADDAW` (word) is **80**.

### IDXLB — `$B8` (66 / 71) and IDXLW — `$BA` (83)

Index a local byte array (add twice the index to TOS). `IDXLB`:

```
IDXLB: INY                     ; 2
       LDA     (IP),Y          ; 5   offset
       STY     IPY             ; 3
       TAY                     ; 2
       LDA     (IFP),Y         ; 5   index
       LDY     #$00            ; 2
       ASL                     ; 2   (index << 1)
       BCC     +               ; 2 not-taken / 3 taken
       INY                     ; 2   (overflow: adjust the high byte)
       CLC                     ; 2
+     ADC     ESTKL,X         ; 4   + TOS low
       STA     ESTKL,X         ; 4
       TYA                     ; 2
       ADC     ESTKH,X         ; 4   + TOS high
       STA     ESTKH,X         ; 4
       LDY     IPY             ; 3
       JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
`IDXLB` = **66** (no overflow) / **71** (overflow).

`IDXLW` (word) reads the index as a word and does `ASL`/`ROL` into `TMP`:
2+5+3+2+5+2+3+2+5+2+3+3+2+4+4+3+4+4+3+22 = **83**.

### IDXAB — `$BC` (79 / 83) and IDXAW — `$BE` (96)

Index an absolute byte array. `IDXAB`:

```
IDXAB: INY                     ; 2
       LDA     (IP),Y          ; 5   addr low
       STA     ESTKH-2,X       ; 4
       INY                     ; 2
       LDA     (IP),Y          ; 5   addr high
       STA     ESTKH-1,X       ; 4
       LDA     (ESTKH-2,X)     ; 5   index (byte at addr)
       STY     IPY             ; 3
       LDY     #$00            ; 2
       ASL                     ; 2   (index << 1)
       BCC     +               ; 2 not-taken / 3 taken
       INY                     ; 2
       CLC                     ; 2
+     ADC     ESTKL,X         ; 4   + TOS low
       STA     ESTKL,X         ; 4
       TYA                     ; 2
       ADC     ESTKH,X         ; 4   + TOS high
       STA     ESTKH,X         ; 4
       LDY     IPY             ; 3
       JMP     NEXTOP          ; 6
; NEXTOP                ; 16
```
`IDXAB` = **79** (no overflow) / **83** (overflow).

`IDXAW` (word) reads the index as a word from the absolute address and does
`ASL`/`ROL` into `TMP`: 2+5+3+2+5+3+3+2+5+2+3+2+5+2+3+3+2+4+4+3+4+4+3+22 = **96**.

## Branches

Two shared routines underlie every relative branch. The "not taken" path is
`NOBRNCH` (advance past the 2-byte offset, continue): **28**. The "taken" path
is `BRNCH` (compute `offset_address + offset` into the IP, then `JMP FETCHOP`):
**63**. So a tested branch is roughly `test (10–21) + 28` (not taken) or
`test + 63` (taken).

| Op | taken | not taken | note |
|----|-------|-----------|------|
| BRNCH `$50` | **63** | — | unconditional; just the taken path |
| BRFLS `$4C` | **75** | **41** | test = `INX`+`LDA`+`ORA`+`BNE` |
| BRTRU `$4E` | **76** | **40** | test = `INX`+`LDA`+`ORA`+`BNE` |
| BREQ `$22` | **88** | 43 / 55 | low differ=43, high differ=55 |
| BRNE `$24` | 78 / 88 | **55** | low differ=78, high differ=88 |
| BRAND `$AC` | **74** | **42** | short-circuit AND; keeps `a` when taken |
| BROR `$AE` | **74** | **42** | short-circuit OR; keeps `a` when taken |
| JUMP `$C4` | **46** | — | absolute, unconditional |
| JUMPZ `$C2` | **59** | **40** | absolute, conditional |

Worked example, `BRFLS` taken:

```
INX                     ; 2   drop the value
LDA     ESTKH-1,X       ; 4   test zero
ORA     ESTKL-1,X       ; 4
BNE     NOBRNCH         ; 2   (not taken → fall into BRNCH)
; BRNCH                 ; 63  compute target, JMP FETCHOP
```
= 2 + 4 + 4 + 2 + 63 = **75**. Not taken: 2 + 4 + 4 + 3 + 28 = **41**.

## FOR-loop branches

These update the loop count (TOS) in place, compare it to the limit (NOS), and
take the branch (`BRNCH`, 63, or `NOBRNCH`, 28). All comparisons are signed.
`INCBRLE` (the others are the same shape) shows the breakdown:

```
INCBRLE: INC     ESTKL,X       ; 5   count += 1
         BNE     _BRLE         ; 2   (not taken unless the low byte wrapped)
         INC     ESTKH,X       ; 5
_BRLE:   LDA     ESTKL+1,X     ; 4   limit - count (signed)
         CMP     ESTKL,X       ; 4
         LDA     ESTKH+1,X     ; 4
         SBC     ESTKH,X       ; 4
         BVS     +             ; 2 not-taken / 3 taken
         BPL     BRNCH         ; 2/3 ; (limit - count >= 0 -> taken)
         BMI     NOBRNCH       ; 2/3
+       BMI     BRNCH         ; 2/3
         BPL     NOBRNCH       ; 2/3
; BRNCH: 63
; NOBRNCH: 28
```
INCBRLE taken: 5+2+16+2+3+63 = **91**. Not taken: 5+2+16+2+2+3+28 = **58**.

The `BRGT`/`BRLT` forms have no update (just the compare); `DECBRGE` uses a
`DEC` (like the `INC` above); `ADDBRLE`/`SUBBRGE` do a full word add/subtract
(~30) instead. The signed-compare block is 21 (taken) / 23 (not taken).

| Op | update | taken | not taken |
|----|--------|-------|-----------|
| BRGT `$A0` | — | **86** | **49** |
| BRLT `$A2` | — | **86** | **49** |
| INCBRLE `$A4` | INC (7) | **91** | **58** |
| ADDBRLE `$A6` | ADD (30) | **114** | **81** |
| DECBRGE `$A8` | DEC (7) | **97** | **64** |
| SUBBRGE `$AA` | SUB (30) | **114** | **81** |

(The overflow/sign-change path of the signed compare adds ~3; not shown.)

## Calls and frames

`CALL`/`ICAL` save the IP, do a 6502 `JSR`, restore the IP, and end with
`JMP FETCHOP` (17). `LEAVE`/`RET` end with `RTS` (the return), so their counts
include the `RTS`. `ENTER` copies the parameters with a per-parameter loop.

### CALL — `$54` (77)

```
CALL: INY                     ; 2
      LDA     (IP),Y          ; 5   addr low
      STA     TMPL            ; 3
      INY                     ; 2
      LDA     (IP),Y          ; 5   addr high
      STA     TMPH            ; 3
      TYA                     ; 2
      SEC                     ; 2
      ADC     IPL             ; 3   save the IP
      PHA                     ; 3
      LDA     IPH             ; 3
      ADC     #$00            ; 2
      PHA                     ; 3
      JSR     JMPTMP          ; 6   the call
      PLA                     ; 4   restore the IP
      STA     IPH             ; 3
      PLA                     ; 4
      STA     IPL             ; 3
      LDY     #$00            ; 2
      JMP     FETCHOP         ; 3
; FETCHOP               ; 14
```
**77** (20 to read the address, 18 to save/restore the IP, 6 for the `JSR`,
19 to dispatch).

### ICAL — `$56` (73)

As CALL, but the address is popped from the stack (16 instead of 20 to get it):
**73**.

### ENTER — `$58` (46 + ~26/param*)

```
ENTER: LDA     IFPL            ; 3   allocate the frame
       INY                     ; 2
       SEC                     ; 2
       SBC     (IP),Y          ; 5   (frame size)
       STA     IFPL            ; 3
       BCS     +               ; 2 not-taken / 3 taken
       DEC     IFPH            ; 5
+     INY                     ; 2
       LDA     (IP),Y          ; 5   (param count)
       BEQ     +               ; 2 not-taken / 3 taken
       ASL                     ; 2
       TAY                     ; 2
-     LDA     ESTKH,X         ; 4   copy param k (high)
       DEY                     ; 2
       STA     (IFP),Y         ; 5
       LDA     ESTKL,X         ; 4   (low)
       INX                     ; 2
       DEY                     ; 2
       STA     (IFP),Y         ; 5
       BNE     -               ; 2   (per param: 26)
+     LDY     #$03            ; 2
       JMP     FETCHOP         ; 3
; FETCHOP               ; 14
```
0 parameters: 3+2+2+5+3+2+2+5+3+2+3+14 = **46**. Each added parameter is **26**.

### LEAVE — `$5A` (23 / 29) and RET — `$5C` (6)

```
LEAVE: LDA     IFPL            ; 3
       INY                     ; 2
       CLC                     ; 2
       ADC     (IP),Y          ; 5   (frame size)
       STA     IFPL            ; 3
       BCS     +               ; 2 not-taken / 3 taken
       RTS                     ; 6
+     INC     IFPH            ; 5
RET:   RTS                     ; 6
```
`LEAVE` no borrow: 3+2+2+5+3+2+6 = **23**; borrow: **29**. `RET` is just `RTS`
= **6**.

### NATV — `$C0` (18 / 24)

```
NATV:  TYA                     ; 2
       SEC                     ; 2
       ADC     IPL             ; 3
       STA     IPL             ; 3
       BCS     +               ; 2 not-taken / 3 taken
       JMP     (IP)            ; 6
+     INC     IPH             ; 5
       JMP     (IP)            ; 6
```
No carry: 2+2+3+3+2+6 = **18**; carry: **24**. (One-way: no return to bytecode.)

## Notes (caveats, outside the tables)

* **Page-cross penalties.** The "typical" counts above assume the relevant
  branches are not taken (same page, no overflow, positive offset, no swap).
  The 6502 takes 1–4 extra cycles when an indirect `LDA`/`STA` or a branch/jump
  crosses a 256-byte page boundary. These are shown per-op where they matter
  (e.g. LW's 55→61, SW's 61→67).
* **Zero-page assumption.** All VM scratch memory is zero-page
  (`plvmzp.inc`), so `zp,X` ops (the stack) cost 4, not 5, and `zp` ops
  (IFPL, DST, TMPL) cost 3, not 4. This is why the counts are lower than an
  absolute-memory version of the VM would be.
* **`BMI -` prologues** (the IP-wrap renormalize) are a rare ~every-256-IP
  overhead and are shown but noted as "not taken normally"; they are not in the
  base count.
* **Variable ops** (`*`) show the assumption for the average; the true cost
  ranges over the operand bit patterns as noted.
