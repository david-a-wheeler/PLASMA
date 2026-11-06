# Notes: unsigned betweenness via CMDSYS calls (research record)

Working notes for the task of analyzing unsigned comparison calls
(`isult`, `isuge`, `isult`, `isule`) as "pseudo-opcodes" and the
betweenness form built on them. The finished analysis goes into
`doc/Bytecodes-cycles.md`, `doc/Bytecodes.md` and `betweenness.md`.

## Step 1: how to call them from our generated system

The routines exist in the CMDSYS library on every target:

* 6502 Apple II: `src/vmsrc/apple/cmd.pla`, `asm uword_isge(a,b)#1`,
  `uword_isle`, `uword_isgt`, `uword_islt` (lines 443-495). The module's
  export table pairs names with code: `word = @uisltstr, @uword_islt`
  and so on, so the export names are `ISUGE`, `ISULE`, `ISUGT`, `ISULT`
  (the `uis*str` byte strings at cmd.pla:103-105).
* Apple 1: `a1cmd.pla`; C64: `src/vmsrc/c64/cmd.pla`; JIT:
  `cmdjit.pla` (all export the same names).
* C VM (plvm): `src/vmsrc/cmdsys.c` exports all four as natives:
  `export_natv("ISUGT", sysisugt)` etc. (lines 876-879).

The compiler already has the whole calling mechanism:

* The front end knows the names and types. `src/parse.c`
  `cmdsys_sigs[]` ends with `"iii", "iii", "iii", "iii"` for
  `isugt isuge isult isule` (each `int, int -> int`); the fixed ids
  are in `src/ppname.c` (`"toupper", "sext", "isugt", "isuge", "isult",
  "isule", "main", "pstr", ...`). A call with no declaration is
  auto-declared from that signature ("the CMDSYS functions ... which
  their first call declares", parse.c header); a declaration must match
  it (error 173 if not).
* The link makes it an import. `src/link.c` `resolve()`: "A used name
  that no object defines must be a CMDSYS function, which becomes an
  import"; the module gets the `CMDSYS` dependency and an ESD entry.
  This is already exercised: the init function that moves globals to
  the heap calls `heapavail`, `isult`, `heapalloc`, `memset`, `puts`
  (link.c `use_cmdsys` and `init_code`).
* The VM resolves the import against the library's exports
  (case-insensitive, 16 chars significant) at module load.

Verified end-to-end with ccish + plvm: `extern int isule(int, int);`
and `return isule(x - 10, 40);` compiles to

```
LLW 0
SUBI 10
CB 40
CALL isule      ; 3 bytes, a relocation to the import
```

and prints the right answer for x = 20, 5, 60, 50, 10 (true is -1,
false is 0; x=5 wraps the subtraction to 65531, which compares above
40 as unsigned, exactly what we want).

The call sequence in the module is: push a (the first argument,
deeper), push b (second, TOS), `CALL addr`, and one word ($FFFF true /
0 false) is left on the stack. That is the same stack shape as
`ISGT`/`ISLE`, so `BRFLS`/`BRTRU` work on the result without change.

## The 6502 bodies (cmd.pla, verified line by line)

**Totals below are superseded:** the per-instruction annotations were
right, but the totals missed one `STA` each; the re-count gives 36/38
cycles, 18/20 bytes (verification log V2/V3).

All four: `zp,X` stack accesses (4 cycles each, per
Bytecodes-cycles.md's timing table), result stored over the second
argument's slot, `INX`, `RTS`.

```
uword_islt(a,b):                 uword_isge(a,b):
LDA  ESTKL+1,X   ; 4  a_lo      LDA  ESTKL+1,X   ; 4  a_lo
CMP  ESTKL,X     ; 4            CMP  ESTKL,X     ; 4
LDA  ESTKH+1,X   ; 4            LDA  ESTKH+1,X   ; 4
SBC  ESTKH,X     ; 4  C iff a>=b SBC  ESTKH,X     ; 4
LDA  #$FF        ; 2            LDA  #$FF        ; 2
ADC  #$00        ; 2  A=C?00:FF ADC  #$00        ; 2
                   STA  ESTKL+1,X ; 4       EOR  #$FF        ; 2
                   STA  ESTKH+1,X ; 4       STA  ESTKL+1,X   ; 4
                   INX             ; 2       STA  ESTKH+1,X   ; 4
                   RTS             ; 6       INX              ; 2
= 32 cycles, 24 bytes              RTS              ; 6
                                    = 34 cycles, 26 bytes
```

* `islt` = (a >= b) ? 0 : $FFFF, i.e. a < b: 32 cycles, 24 bytes.
* `isgt` = NOT(b >= a), i.e. a > b (no `EOR`): 32 cycles, 24 bytes.
* `isge` = (a >= b) ? $FFFF : 0: 34 cycles, 26 bytes.
* `isle` = (b >= a) ? $FFFF : 0, i.e. a <= b (has the `EOR`): 34
  cycles, 26 bytes.

The bodies live in the library, not the module: the module's cost is
the 3-byte `CALL` each time, plus the cycles.

## Step 2 finding: CALL costs 83, not the 77 the tables give

(Still correct, and now in `doc/Bytecodes-cycles.md`. The 115/117
totals below are superseded by 119/121, verification log V3.)

`Bytecodes-cycles.md`'s reviewed CALL count (77) traces the handler
line by line but misses the trampoline. `JSR JMPTMP` (6) jumps to the
`$4C` byte the VM installed at start (`LDA #$4C; STA JMPTMP`,
plvm02.s:213-216), which is `JMP (TMPL)` (6): the standard
indirect-JSR. Nothing else writes JMPTMP. So a call to native code is:

20 (read the address) + 18 (save the IP) + 6 (JSR JMPTMP) + 6
(JMP (TMPL)) + 14 (restore the IP) + 2 (LDY #$00) + 3 (JMP FETCHOP)
+ 14 (FETCHOP) = **83**. ICAL is the same shape with the address
popped (16 instead of 20): **79**, not 73.

The 6 cycles are real and are not counted anywhere else: a native
target has no entry code of its own, and the return side is the
target's `RTS` (6), which is in the target's count.

Pseudo-op totals (CALL 83 + body, body includes the RTS):

| pseudo-op | stack effect | module size | cycles |
|-----------|--------------|-------------|--------|
| `isult(a,b)` | (a b -- a<b)  | 3 (the CALL) | 83 + 32 = 115 |
| `isuge(a,b)` | (a b -- a>b)  | 3          | 83 + 34 = 117 |
| `isugt(a,b)` | (a b -- a>b)  | 3          | 83 + 32 = 115 |
| `isule(a,b)` | (a b -- a<=b) | 3          | 83 + 34 = 117 |

isult/isgt are 32; isule/isge are 34.

## Betweenness form and its numbers

(The 117-cycle call and the 339/258/333, 333.6, 334/253/328, 328.6
figures below are superseded: the call is 121, giving 343/262/337,
337.6, and 338/257/332, 332.6; verification log V3/V6. The byte count
11 and the reproduction of the existing table's rows are still
correct.)

The Hacker's Delight form with the call (c1 = 10, c2 = 50):

```
LLW 0
SUBI 10
CB 40
CALL isule
BRFLS L
```

Bytes (the table convention: every op at full size, the final branch
counted as 2): 2 + 2 + 2 + 3 + 2 = **11**.

Cycles, using the exact per-op counts that reproduce every existing
row of betweenness.md's table (ISGE/ISLE 55 true / 53 false, SUBI
41/47, BRFLS 41 not-taken / 75 taken counted only when taken, LLW 59,
CB 41):

* x < c1:  59 + 47 + 41 + 117 + 75 = **339**
* in:      59 + 41 + 41 + 117       = **258**
* x > c2:  59 + 41 + 41 + 117 + 75 = **333**
* average (8 in, 124 below, 124 above): (8*258 + 124*339 + 124*333)
  / 256 = 85392 / 256 = **333.6**

Char variant (LLB 54): 334 / 253 / 328, average 328.6.

For reference, the model reproduces the existing table exactly:
Obvious 223/346/419, IS_BETWEEN 329/256/329, XOR 374/295/368, native
hypothetical 275/196/269, byte aligned 216/193/216 (BRNE not-taken
planning figure 55).

## Current state of the unsigned case (baseline)

ccish rewrites the *signed* betweenness in the IR pass (irin.c
`between_rewrite`, commit 84df5b55): `x >= c1 && x <= c2` becomes
`LLx, CW (MAX-c2), ADD, CW (MAX-c2+c1), ISGE, BRFLS` (12 bytes,
329/256/329). Its comment says unsigned compares never reach it: they
are sign-bit flipped before they are held. Today an *unsigned*
betweenness (`(unsigned)x >= 10 && (unsigned)x <= 50`) compiles to the
naive two-comparison form, 25 bytes (26 at full size; the "24" here was
an arithmetic slip, verification log V3.3):

```
LLW 0   CW 32768   XOR   CW 32778   ISGE   BRFLS L
LLW 0   CW 32768   XOR   CW 32818   ISLE   BRFLS L
```

So the call form (11 bytes, 343/262/337, corrected; the 339/258/333
above are superseded, V3) would replace, for the unsigned case, either
that naive form (25 bytes, slower on average: 476.8 against 337.6,
though 16 cycles faster in the x < c1 early-out case) or the
programmer's Hacker's Delight macro with the sign-flip emulation
(14 bytes, 374/295/368).

## What an implementation needs (step 5)

1. No new declarations: `isult`/`isuge`/`isult`/`isule` are already
   CMDSYS names; a call auto-declares them (signature `int (int, int)`),
   or a program may declare `extern int isule(int, int);` (checked,
   error 173 on mismatch).
2. No new link machinery: the import/ESD/CMDSYS-dependency path exists
   and is already used by the init function's `isult` call.
3. The rewrite: in irin.c, extend `between_rewrite` (or add a sibling)
   to the unsigned shape. The unsigned compares carry the pending
   sign-bit flip, which is what keeps the signed matcher from seeing
   them; the new matcher must recognize the flipped shape (load, flip,
   const, ISGE/ISLE, BRFALSE twice to the same label) and emit
   LOAD, SUB/CONST c1, CONST (c2-c1), CALL isule, BRFALSE instead.
   Alternatively the C macro `IS_BETWEEN_U` written with `isule` needs
   no compiler change at all: it compiles today (verified).
4. True value: the library returns $FFFF/0, the same as the VM's
   compares, so BRFLS/BRTRU need no change.
5. Cost: one 3-byte CALL per check instead of the 2-4 bytes of a
   sign-flipped compare, and 121 cycles (not the 117 above; V3) instead
   of about 150 for the flipped ISLE (46 CW + 48 XOR + 53/55 ISLE); the
   module gains no
   code of its own, but the program must run on a system with the
   CMDSYS library (true of every PLASMA target here).

## Verification log (second pass, 2026-11-06)

Handoff steps 1-5 executed in this pass. Every number was re-verified
against the source before it was written into the docs; the specific
instructions being counted are recorded here so the totals can be
re-counted from this record alone. **The handoff contained arithmetic
errors in the body totals (see V3); the docs use the corrected numbers.**

### V1. CALL = 83, ICAL = 79 (the $4C trampoline), verified

Source: `plvm02.s:1380-1398` (CALL), `:1403-1420` (ICAL), `:213-216`
(`LDA #$4C` / `STA JMPTMP`), `plvmzp.inc:26-28` (`JMPTMP = ESP+1`,
`TMP = JMPTMP+1`, `TMPL = TMP`), `plvm02.s:35` (`FETCHOP = NEXTOP+1`),
`plvm01.s:1028` (comment showing the trick). Confirmed the only write
to `JMPTMP` in plvm02.s is line 216.

`JSR JMPTMP` does not jump to the target: the three bytes at JMPTMP
form a `JMP (TMPL)` instruction (the `$4C` opcode installed at start,
operand the zero-page `TMPL/TMPH`), which then jumps to the target.
Every native call pays the trampoline.

CALL (`$54`), per instruction:

```
INY            2   LDA (IP),Y   5   STA TMPL    3
INY            2   LDA (IP),Y   5   STA TMPH    3    ; 20: read the address
TYA            2   SEC          2   ADC IPL     3
PHA            3   LDA IPH      3   ADC #$00    2
PHA            3                                  ; 18: save the IP
JSR JMPTMP     6                                  ;  6: the call
JMP (TMPL)     6        (the trampoline at JMPTMP);  6
PLA            4   STA IPH      3
PLA            4   STA IPL      3                 ; 14: restore the IP
LDY #$00       2   JMP FETCHOP  3
FETCHOP        14       (= NEXTOP 16 minus its INY 2)
```
20 + 18 + 6 + 6 + 14 + 2 + 3 + 14 = **83**.

ICAL (`$56`): the address is popped instead, `LDA ESTKL,X 4, STA TMPL
3, LDA ESTKH,X 4, STA TMPH 3, INX 2` = 16, then the same
18 + 6 + 6 + 14 + 2 + 3 + 14 = **79**.

The 77/73 in Bytecodes-cycles.md's "Calls and frames" counted the
handler without the trampoline (77 = 83 - 6). The 6 are not counted
anywhere else: a native target has no entry code of its own, and the
return (the target's `RTS`, 6) belongs to the target's count.

### V2. The four 6502 bodies (cmd.pla), verified and re-counted

Source: `src/vmsrc/apple/cmd.pla`: `uword_isge` (line 443),
`uword_isle`, `uword_isgt`, `uword_islt`; export strings at
`cmd.pla:103-105` (`ISUGT`, `ISUGE`, `ISULT`, `ISULE`); the export
words pair each string with its body. All stack accesses are `zp,X`
(4 cycles; `ESTKL=$C0`/`ESTKH=$A0` per `plvmzp.inc`), immediates 2,
`INX` 2, `RTS` 6, per the doc's own timing table.

The common shape (10 instructions, no EOR), shown for `uword_islt`
(a - b):

```
LDA  a-lo      zp,X      4     ; a = first arg, one deep
CMP  b-lo      zp,X      4
LDA  a-hi      zp,X      4
SBC  b-hi      zp,X      4     ; C iff a >= b (unsigned)
LDA  #$FF      #imm      2
ADC  #$00      #imm      2     ; A = C ? $00 : $FF
STA  a-lo      zp,X      4
STA  a-hi      zp,X      4
INX                  acc      2   ; pop b; a's slot now holds the result
RTS                   (6)          ; returns into the CALL handler
```
islt: 4+4+4+4 + 2+2 + 4+4 + 2 + 6 = **36 cycles**; bytes 2+2+2+2+2+2+
2+2+1+1 = **18**.

The other three bodies are the same shape with two independent knobs:
the compare direction (which operand is loaded first) and the `EOR
#$FF` after the `ADC`. The `LDA #$FF` / `ADC #$00` pair turns the
carry into `$00` (C set) or `$FFFF` (C clear); the `EOR` inverts it.

| body  | computes  | carry is  | EOR | result  | cycles | bytes |
|-------|-----------|-----------|-----|---------|--------|-------|
| islt  | a - b     | a >= b    | no  | a < b   | 36     | 18    |
| isgt  | b - a     | b >= a    | no  | a > b   | 36     | 18    |
| isge  | a - b     | a >= b    | yes | a >= b  | 38     | 20    |
| isle  | b - a     | b >= a    | yes | a <= b  | 38     | 20    |

The listing above (a - b, no EOR) is `uword_islt`. `uword_isge` is
the same listing plus the `EOR #$FF`. `uword_isgt` and `uword_isle`
swap the two operands of the compare (load b first): `LDA ESTKL,X` /
`CMP ESTKL+1,X` / `LDA ESTKH,X` / `SBC ESTKH+1,X`.

All instructions are unconditional: the body costs the same for a true
or a false result. The result is stored over the first argument's slot
and `INX` pops the second, so one word ($FFFF/0) is left on the stack.
The body's `RTS` returns into the CALL handler (the caller's `JSR`),
so a pseudo-op's total is CALL 83 + the body (RTS included):

* `isult`/`isugt` = 83 + 36 = **119** (module size: the 3-byte CALL)
* `isuge`/`isule` = 83 + 38 = **121** (module size: the 3-byte CALL)

### V3. Discrepancies found in the handoff (why the numbers changed)

1. **Body totals.** The handoff's "6502 bodies" section lists the
   instructions with correct per-instruction cycles (4,4,4,4,2,2[,2],
   4,4,2,6) but totals them to 32/34; those annotations sum to 36/38.
   Each total is short by exactly one `STA` (4 cycles). The handoff's
   byte totals (24/26) are 6 over the real 18/20. The source was
   re-read and re-counted: **36/38 cycles, 18/20 bytes** (V2).
   Downstream: the pseudo-op totals are **119/121, not 115/117**.
2. **Betweenness call-form numbers** (recomputed with 121, same per-op
   counts and sequence convention as V5): word **343 / 262 / 337,
   average 337.6** (not 339/258/333, 333.6); char **338 / 257 / 332,
   average 332.6** (not 334/253/328, 328.6).
3. **Naive unsigned form's byte count.** The handoff says "24 bytes";
   the two comparisons are 13 bytes each (LLW 2, CW 3, XOR 1, CW 3,
   ISGE/ISLE 1, BRFLS 3), 26 full-size, **25** under the table
   convention (the final branch counted as 2). Its cycles, same
   convention: word **327 / 549 / 622**. x<c1 bails after the first
   compare: 59+46+48+46+53+75. In-range adds the second compare and
   ends with the final BRFLS not taken (not counted): 59+46+48+46+
   55+41 + 59+46+48+46+55. x>c2: the second compare fails and its
   branch is taken: 59+46+48+46+55+41 + 59+46+48+46+53+75. Char is
   -5 per load (LLB for LLW; the x<c1 case has one load, the other
   two have two): 322 / 539 / 612.

### V4. Compile/run verification (scratch files recreated)

The handoff's t3.c/t6.c were gone from `ccish/tests/out/bt/`; they
were recreated there (t1.c, t3.c, t6.c, t7.c, t8.c) and run with the
built `ccish` and `src/plvm`:

* **t1.c** (`if (x >= 10 && x <= 50)`): the signed rewrite fires,
  `DLW 0 / CW $7FCD (32717) / ADD / CW $7FD7 (32727) / ISGE / BRFLS`
  (the `LLW` is a `DLW` because the peephole folds the initializer's
  store-load pair; the check shape is as documented).
* **t3.c** (`if ((unsigned)x >= 10 && (unsigned)x <= 50)`): no
  rewrite; the naive two sign-flipped compares, exactly as the handoff
  shows: `CW $8000 / XOR / CW $800A / ISGE / BRFLS` then `LLW 0 / CW
  $8000 / XOR / CW $8032 / ISLE / BRFLS`.
* **t6.c** (`if (isule(x - 10, 40))`): `SUBI 10 / CB 40 / CALL isule
  / BRFLS`; the CALL is 3 bytes with a relocation to the import (the
  listing shows `CALL $0000 ; isule (import)`, patched at load).
* **t8.c** (fixed values): `isule(10,40) = -1`, `isule(-5,40) = 0`
  (x=5 wraps x-10 to 65531, above 40 unsigned, as intended),
  `isule(50,40) = 0`, `isule(40,40) = -1`, `isule(0,40) = -1`. True
  is $FFFF (-1), false 0, the same shape as the VM's compares, so
  `BRFLS`/`BRTRU` work unchanged.
* **t7.c** (sweep): for every x in 0..80 and 65525..65535, the truth
  of `isule(x - 10, 40)` equals the truth of `(unsigned)x >= 10 &&
  (unsigned)x <= 50`; zero mismatches (the run prints only `done`).

Side finding, worth keeping: `puts` takes only `pstr("...")` (the
ptest.h contract: PLASMA strings are length-prefixed, a bare C literal
pointer is not one). A first t7 draft passed `"done\n"` directly and
`prstr` read the `d` (0x64) as a 100-byte length, printing `one` plus
96 bytes of module code. That is the contract working as designed, not
a compiler bug; no test in the suite uses `puts` on a bare literal.

### V5. The two counting conventions, verified against the table

* **Bytecodes docs**: the "Cycles" column is **one opcode**, its
  handler plus the trailing dispatch (JMP NEXTOP 22 / JMP DROP 24 /
  inlined 18). The new "System calls" section uses the same unit: one
  `CALL` op executing a native routine (V1 + V2 = 119/121), plus the
  corrected CALL 83 / ICAL 79.
* **betweenness.md**: each row is a **sequence** of those per-op
  totals for a fixed listing, with: bytes at full size but the final
  branch counted as 2; comparisons 55 true / 53 false; and the final
  branch's cycles counted **only when taken** (the listing ends where
  the true path continues, so its not-taken cost belongs to the next
  statement).

Verified by reproducing every existing row from the per-op counts
(LLW 59, LLB 54, CN 36, CB 41, CW 46, XOR 48, ADD 50, SUBI 41/47,
ISGE/ISLE 55/53, BRFLS 75 taken / 41 not-taken, ANDI 43, BRNE 78
taken / 55 not-taken, DECR 33): Obvious 223/346/419 (char is -5 per
load), IS_BETWEEN 329/256/329, IS_BETWEEN_U 374/295/368, native
hypothetical 275/196/269 (ISULE assumed 55/53), and the averages
321.8 / 326.7 / 368.6 / 269.6.

One pre-existing wrinkle, now documented in betweenness.md: the three
byte rows (216/193/216, 257/234/257, 249/226/249) reproduce only when
the final BRNE **not-taken (55) is included** in the in-range case,
whereas every int row excludes the final BRFLS not-taken (41). The new
row follows the handoff's instruction and the int-row convention; the
in-range figures differ from a byte row's by exactly that constant 55
choice.

### V5a. Pre-existing typo fixed in Bytecodes.md

The `ADD` row of the "Arithmetic and logic" table had two stray cells (a
plain dash and a 1, inserted after the 50), making it 9 columns against
the 7-column header; removed. Not related to the unsigned work; found
while checking the edited tables' well-formedness.

### V6. Corrected numbers used in the docs

Pseudo-ops (Bytecodes.md, Bytecodes-cycles.md): `isult`/`isugt` 3
bytes, **119**; `isuge`/`isule` 3 bytes, **121**. CALL **83**, ICAL
**79** (corrections of 77/73).

Call-form betweenness (betweenness.md, c1 = 10, c2 = 50; word uses
LLW 59 / SUBI 47 or 41 / CB 41 / CALL isule 121 / BRFLS 75 when taken):

* word: x<c1: 59+47+41+121+75 = **343**; in: 59+41+41+121 = **262**;
  x>c2: 59+41+41+121+75 = **337**; bytes 2+2+2+3+2 = **11**.
  Average (8 in, 124 below, 124 above):
  (8*262 + 124*343 + 124*337)/256 = 86416/256 = **337.6**.
* char (LLB 54): 54+47+41+121+75 = **338**; 54+41+41+121 = **257**;
  54+41+41+121+75 = **332**; bytes **11**; average (8*257 +
  124*338 + 124*332)/256 = 85136/256 = **332.6**.

The call form still beats both existing unsigned alternatives on both
axes: the naive 25-byte two-compare lowering (327/549/622, V3.3) and
the 14-byte sign-flipped macro (374/295/368). The hypothetical native
unsigned compare would still beat the call form (9 bytes, 275/196/
269); the call form is what the system can do today, and it is the
argument for adding the native op.

## Remaining steps (handoff), done 2026-11-06

All five steps were executed in the second pass; the verification log
above records what was checked. The docs use the corrected numbers
(119/121, 343/262/337, 337.6; char 338/257/332, 332.6).

1. `doc/Bytecodes-cycles.md`: done. New "System calls: the CMDSYS
   unsigned compares" section after "Calls and frames" (bodies with
   cycle listings, totals 119/121); CALL 77->83 and ICAL 73->79 in
   place, flagged as a correction, with the trampoline traced
   (plvm02.s:213-216, plvmzp.inc:26-28); correction noted in the
   file's header.
2. `doc/Bytecodes.md`: done. Four pseudo-op rows in the "Calls and
   frames" table (size 3, cycles 119/121/119/121); CALL/ICAL rows
   corrected to 83/79 with a note explaining the trampoline and the
   pseudo-ops.
3. `ccish/doc/betweenness.md`: done. "IS_BETWEEN_U via a CMDSYS call"
   after "Standard solution" (listing, 11 bytes, 343/262/337 int and
   338/257/332 char, averages 337.6/332.6, two rows in the final table,
   the implementation discussion, the trade against the hypothetical
   native op); "Trade-offs" and the Bottom line updated to point at the
   call form; the stale "next section" reference in "Standard solution"
   fixed.
4. Table convention: verified (V5) with one finding added to the doc:
   the existing byte rows include the final BRNE not-taken (55) in the
   in-range case while the int rows exclude the final BRFLS not-taken
   (41); the new rows follow the int-row convention as instructed.
5. Scratch tests: t3.c/t6.c were gone and were recreated in
   `ccish/tests/out/bt/` (t1, t3, t6, plus t7 sweep and t8 fixed-value
   run), all verified against `plvm` (V4).

The original steps, for the record:

1. `doc/Bytecodes-cycles.md`: add a "System calls" section (after the
   Calls section): the four pseudo-ops, the 6502 bodies with cycle
   listings, the CALL=83/ICAL=79 correction (the $4C JMP (TMPL)
   trampoline after JSR JMPTMP, plvm02.s:213-216/1393/1415), and the
   totals 115/117/115/117. Update the CALL (77 to 83) and ICAL (73 to
   79) entries there, flagged as a correction.
2. `doc/Bytecodes.md`: add the four pseudo-op rows to the table (size
   3, cycles 115/117/115/117) and fix the CALL/ICAL cycle entries the
   same way.
3. `ccish/doc/betweenness.md`: new section "IS_BETWEEN_U via CMDSYS
   call" with the listing (LLW 0 / SUBI 10 / CB 40 / CALL isule /
   BRFLS), 11 bytes, 339/258/333, average 333.6 (char: 11 bytes, 334/
   253/328, average 328.6); new row in the final table; the
   implementation discussion (it beats the current unsigned lowering,
   which is the naive 24-byte two-comparison form, and the 14-byte
   sign-flipped macro form; the C macro needs no compiler change,
   verified; the IR rewrite is the optional follow-up).
4. The table convention, verified: every op at full size, the final
   branch counted as 2, comparisons 55 true / 53 false, the last
   BRFLS counted only when taken. Use it for the new row.
5. Scratch test files are in ccish/tests/out/bt/ (t3.c the current
   unsigned output, t6.c the working isule call).
