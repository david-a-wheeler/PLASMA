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
naive two-comparison form, 24 bytes:

```
LLW 0   CW 32768   XOR   CW 32778   ISGE   BRFLS L
LLW 0   CW 32768   XOR   CW 32818   ISLE   BRFLS L
```

So the call form (11 bytes, 339/258/333) would replace, for the
unsigned case, either that naive form (24 bytes, much slower) or the
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
   sign-flipped compare, and 117 cycles instead of about 150 for the
   flipped ISLE (46 CW + 48 XOR + 53/55 ISLE); the module gains no
   code of its own, but the program must run on a system with the
   CMDSYS library (true of every PLASMA target here).

## Remaining steps (handoff)

1. `doc/Bytecodes-cycles.md`: add a "System calls" section (after the
   Calls section): the four pseudo-ops, the 6502 bodies with cycle
   listings, the CALL=83/ICAL=79 correction (the $4C JMP (TMPL)
   trampoline after JSR JMPTMP, plvm02.s:213-216/1393/1415), and the
   totals 115/117/115/117. Update the CALL (77 to 83) and ICAL (73 to
   79) entries there, flagged as a correction.
2. `doc/Bytecodes.md`: add the four pseudo-op rows to the table (size
   3, cycles 115/117/115/117) and fix the CALL/ICAL cycle entries the
   same way.
3. `ccomp/doc/betweenness.md`: new section "IS_BETWEEN_U via CMDSYS
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
5. Scratch test files are in ccomp/tests/out/bt/ (t3.c the current
   unsigned output, t6.c the working isule call).
