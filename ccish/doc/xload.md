# XLOAD: a second-stage loader for programs too big to load

XLOAD lets a 64K Apple II run a PLASMA program that fits in memory
while running but can't be loaded, because PLASMA's loader reads the
whole module file, relocation data included, before anything runs.
ccish's front end, cccomp, is such a program. CMD loads XLOAD, a small
ordinary module; XLOAD gets out of the way, loads the program's
**image** (which ccrel writes with `-x`) in place, applies its fixups
as they stream in from the disk, and hands off to it. PLASMA itself
doesn't change.

This document says what XLOAD must do and how. Why we chose it, the
options we set aside, and how PLASMA's own loader works are in
[streaming-options.md](streaming-options.md) (cited below as "options"
plus its fact or section number).

**Status (2026-11-12).** The image format (section 3) is done: ccrel
and ccish write it with `-x`, and tools/xlcheck.py checks each image
against the REL module of the same link (tests/special/xload.sh, in
`make test`). XLOAD itself isn't written. Nothing here has run on an
Apple II.

## 1. Requirements

- R1. **cccomp loads and runs on a 64K Apple II**, with today's
  PLASMA: no change to CMD, the VM or the system disk.
- R2. **One image works on the 64K and the 128K Apple II** (PLVM.128),
  XLOAD handling the difference quietly (the user, 2026-11-12), and
  under plvm, for the tests.
- R3. **Reuse existing mechanisms** as much as possible: CMDSYS's
  exports, FILEIO, and the stock loaders' own rules.
- R4. **Nothing of XLOAD stays resident** while the program runs:
  cccomp has under 1K to spare running.
- R5. **ccish writes either a REL module or an image**, by a flag
  (`-x`). REL stays the default, for everything but the programs that
  need XLOAD.

## 2. What the user sees

```
+XLOAD CCCOMP FOO
```

where today it would be `+CCCOMP FOO`. XLOAD is a prefix, one loader
for any program written as an image: one copy on the disk, one thing
to test. (The name was chosen from PRELOAD, GO, LAUNCH, XLOAD, STREAM
and FETCH; "RUN" is BASIC's, and "GO" is common elsewhere.)

- **Arguments.** lib/start.c builds `argv` by splitting CMDSYS's
  command line in place, so XLOAD takes its own word off the front of
  that buffer before the hand-off: the program sees `CCCOMP FOO`, and
  its `argv[0]` is `CCCOMP`, as now.
- **Files.** The image is a file of its own: loadmod reads a module's
  file to its end (options, fact 12), so XLOAD can't carry it. It can
  keep the plain name `CCCOMP`, with a ProDOS type other than $FE, so
  that `+CCCOMP` without the prefix gets CMD's "incompatible type"
  error (options, fact 9) rather than a load that overwrites memory.
  XLOAD checks the type too. Which type is still to choose.
- **Which programs.** cccomp first, since it's the one that can't
  load. ccpp, ccgen and ccrel load today, and can stay REL modules
  until XLOAD works on MAME; as images they'd save disk (section 6).

## 3. The image format (version 3)

### 3.1 Why it differs from PLASMA's REL format

ccrel writes the image and XLOAD reads it; nothing else need
understand it. So it's shaped for what XLOAD must do, and only that,
not to match a REL module:

- **It streams.** A REL module is meant to be read whole, then its
  relocation directory (RLD) applied from memory. Its imports' names
  (the ESD) come after the RLD, so a loader reading it in order meets
  an external fixup before the name it needs; and the stock loaders
  find a function's def entry by a linear search for each fixup. The
  image puts the import names before the body, so XLOAD resolves them
  first, and then applies each fixup as it's read, in address order,
  holding nothing but a small buffer.
- **It's VM-neutral.** A REL module's fixups point into the bytecode,
  and each loader turns that into a def entry of its VM's form. The
  image refers to functions by number, and XLOAD builds the entries:
  5 bytes on the 64K Apple and plvm, 7 on the 128K Apple, whose
  bytecode also moves to aux memory (R2; section 4.3).
- **It's small, so it loads fast and fits on the disk.** About one
  byte per fixup instead of four; a 2-byte table entry per function
  instead of a 4-byte RLD record; and the data segment's zeros stored
  as counts (PLASMA request 12, doc/plasma-requests.md). cccomp's
  image file is 30,683 bytes against its 43,799-byte REL module.
- **It holds only what ccish's programs use.** No exports (ccish
  modules export nothing), no 8-bit fixups, and one kind of body.

### 3.2 The layout

ccrel writes it with `-x` (link.c; the `ccish` driver passes `-x` to
the same code). Words are little-endian, and every address in it is
an offset from the body's start, where XLOAD loads it.

| Bytes | What |
|---|---|
| 2 | Magic: `X` `L` ($58 $4C) |
| 1 | Version: 3 |
| 1 | Flags: 0 (none yet; a "no JIT" bit, as a REL header's, could go here) |
| 2 | The body's length |
| 2 | Where its bytecode starts: everything before is data and strings |
| 2 | The functions, init's included (the last) |
| any | Dependencies: module names in DCI form, as a REL module's, then $00 |
| any | Imports: names in DCI form, numbered from 0, then $00 |
| 2 each | The function table: each function's offset, in address order, init last |
| any | The body's data segment (data, strings, pad), as runs: below |
| length less that | The body's bytecode, as is |
| any | The fixups, then $FF |

- **The body** is exactly the REL module's (data, strings, a pad byte,
  bytecode, helpers, init), relocated for address 0, except that every
  reference to a function (a call, or a function pointer in code or
  data) holds the function's number in the function table. **The
  image holds no def entries:** XLOAD builds them, in the form the VM
  it's running on wants (section 4.3), and each such word becomes its
  entry's address. So one image serves every VM whose bytecode is the
  same (R2).
- **The data segment is stored as runs,** which XLOAD expands as it
  reads them, until they fill the data segment (the header's bytecode
  offset):
  - $01-$7F: that many literal bytes follow.
  - $81-$FF: that many, less $80, zero bytes (1-127).
  - $80 lo hi: the word's count of zero bytes.
  - $00 isn't used.

  ccrel writes a zero run for 3 or more zeros; 2 cost no more inside a
  literal run. This is PLASMA request 12 (doc/plasma-requests.md),
  done in the image: most of each program's data is zeros, the
  zero-filled tables too small to move to the heap. The bytecode
  isn't run-coded: its zero runs are short, so the framing would cost
  more than it saves (request 12 measured this).
- **The function table is in address order,** so that each function's
  size is the distance to the next one's start, as the 128K loader
  uses to decide which functions the JIT may compile; and so that
  nothing needs a search (CMD's lookupdef is a linear search for each
  such fixup).
- **The fixups,** in address order. Each gives the gap from the end of
  the word before (from 0, for the first), and its kind:
  - $00-$3E: a gap of 0-62; add the load address. $3F lo hi: the
    same, with the gap as a word.
  - $40-$7E: a gap of b - $40; the word holds a function's number:
    write its def entry's address. $7F lo hi: the same, with the gap
    as a word.
  - $80-$FE: import number b - $80 (so at most 127 imports), then a
    gap byte ($00-$FE, or $FF lo hi); add the import's address, from
    `lookupsym`. The word holds an addend, as in a REL module.
  - $FF: the end.

  The data's come first, then the code's, object by object, then
  init's. They come out of ccgen in order with no sorting; ccrel stops
  with an error if they ever don't.
- **No absolute bytecode addresses in the body.** Branches are
  relative, ccgen never emits JUMP (options, fact 15), and functions
  are reached only through def entries. So the bytecode can run at any
  address, in main or aux memory, with only the def entries knowing
  where it is.
- **Limits:** an image needs `main` (there's nothing else to run), at
  most 127 imports, and at most `MAX_DEFS` (320) functions.
- **Dependencies** are only checked: XLOAD can't load a module itself
  (it would load where the program goes), so each must already be in
  the symbol table, as FILEIO is, being XLOAD's own dependency.
- **Possible later:** the "no JIT" flag. And since zeros now cost the
  image almost nothing, ccrel could leave zero-filled globals in the
  image rather than moving them to the heap (approach.md, "Large
  zero-filled data on the heap"): each access would be an LA, not an
  LAW of a pointer word (about 24 cycles less on the 6502), and the
  pointer words and init's allocation code would go. The memory used
  is the same either way. Not decided.

**Earlier versions,** both 2026-11-12 and never used: version 1
(bc34918a) put a table of 5-byte `JSR $03D6` def entries in the image,
which fit only the 64K VM, so version 2 (8e3006d3) replaced it with
the function table, for R2; version 3 added the data segment's runs.

**Checking it.** tools/xlcheck.py reads an image and the REL module of
the same link as their loaders would, for the same load address, and
requires that they describe the same program: the same bytes (the
data segment's runs expanded), the same words relocated, each to the same thing (a REL word pointing into
the bytecode, and an image word with a "def" fixup, must name the same
function). It was written from this description, not from ccrel's
code, so it doesn't share ccrel's mistakes. tests/special/xload.sh
runs it on five run tests and ccish's four programs, checks that
ccrel-small writes the same images, and checks the no-main and
`MAX_DEFS` errors.

## 4. What XLOAD must do

XLOAD's file is an ordinary $6502 module in two parts:

1. **The first part,** a few hundred bytes, is all loadmod runs: a
   small relocator.
2. **The second part,** the real loader, is carried inside the first
   part's data as an image in section 3's format. loadmod sees only
   bytes. Once relocated, it's ordinary ccish C: globals, strings,
   helpers and calls all work.

### 4.1 The steps

1. loadmod loads XLOAD (and FILEIO, its dependency) and calls its
   init, which calls the first part's `main` directly.
2. The first part captures the VM's state for the hand-off (4.5),
   tells which VM it's on (4.2), and reads the program's header for
   its size.
3. It places the second part just above where the program will end,
   builds its def entries (4.3), applies its fixups, looks up its
   imports with `lookupsym`, and calls it. On the 128K VM it also moves
   the second part's bytecode to aux (4.4).
4. The second part checks the dependencies, looks up the imports, and
   reads the program's body straight to its final place: the bottom of
   the heap, where XLOAD's own module starts, over the first part. It
   reads in chunks through a small buffer, expanding the data
   segment's runs into place and copying the bytecode, then streams
   the fixups, applying each as it comes.
5. It builds the program's def entries (4.3), and on the 128K VM moves
   its bytecode to aux (4.4).
6. It closes its files and sets the heap to the end of the program's
   memory (`heaprelease`): the body's end, or on the 128K VM, the
   body's bytecode offset. The program's init takes its moved globals
   from `heapalloc` (link.c, init_code), as after loadmod, and nothing
   may allocate after this, since the second part's code is above,
   now free.
7. The hand-off (4.5) jumps to the program's init. Nothing of XLOAD
   stays (R4).

**Why load in place:** the alternative, reading and fixing up the
program higher up and block-moving it down, would add about 16 cycles
a byte, about half a second for cccomp's 31.5K at 1 MHz. "Apples
aren't fast." Loading in place avoids it, and keeps the hand-off to
restoring state and a jump.

**Where the second part goes:** above the program's end, so that its
frames grow from the current frame pointer as usual. The program's
heap tables are later allocated over it. If memory is tighter than
expected, the fallback is just below the frame stack, which needs the
frame pointer lowered first, another piece of native code.

### 4.2 Which VM is running

PLASMA.SYSTEM (plvm02.s:48-62, :1500-1542) checks ProDOS's MACHID for
128K and then tries another VM: first the 65802/65816 VM (PLVM16,
plvm802.s), then PLVM.128 (plvmjit02.s, with CMDJIT, cmdjit.pla). It
falls back to the 64K VM if neither file is on the disk. So the
memory size doesn't say which VM is running; XLOAD asks the VM itself.

The vector in a def entry alone doesn't tell: $03D6 is the 64K VM's
indirect entry and also the 128K VM's JIT entry. The entries' spacing
does, since a loader lays out a module's entries consecutively. If
XLOAD's first file defines two small functions as its first two
symbols, ccrel's RLD gives them consecutive entries, and the
difference of their addresses is the entry size:

| Spacing | Vector | VM |
|---|---|---|
| 5 | `JSR $03D6` | the 64K Apple VM |
| 5 | `JSR $FF0C` | plvm |
| 7 | `JSR $03DC` or `JSR $03D6` | the 128K Apple VM |
| anything else | | refuse, with a message (the 65816 VM, the Apple ///) |

A listing test can pin that the two entries are consecutive.

### 4.3 Building def entries

As each VM's own loader does, so that the VM treats the program as it
would a REL module (R3):

- **64K VM:** 5 bytes, `JSR $03D6` and the bytecode's address in main
  memory, as cmd.pla's adddef.
- **plvm:** 5 bytes, `JSR $FF08` and the address. plvm's own loader
  copies bytecode out to host memory and uses `JSR $FF0C` with a
  handle (src/vmsrc/cmdsys.c, add_def and load_mod), but its $FF08
  (VM_INDIRECT_ENTRY, plvm.c's vm_iidef) runs bytecode in the 6502
  memory at the entry's address word, as the 64K VM's $03D6 does.
- **128K VM:** 7 bytes, as cmdjit.pla's adddef (:1056-1074, the
  t_defentry struct :55-61): `JSR`, the vector, the bytecode's
  address in aux, a call countdown, and the size. The vector is $03DC
  (interpreted from aux), or $03D6 (the JIT's entry) with `jitcount`
  as the countdown and the size, when the size (the next function's
  start less this one's) is at most `jitsize`. `jitcount` and
  `jitsize` are in CMDSYS's exported table (0 until the JIT module,
  loaded at startup, sets them to 32 and 255, libsrc/apple/jit.pla:
  52-53), so XLOAD applies CMDJIT's rule with CMDJIT's settings. An
  entry after the last one has a 0 first byte, as adddef leaves it.

Each "def" fixup's word then becomes the address of the entry it
names.

### 4.4 The 128K VM: bytecode in aux memory

CMDJIT's loadmod reads and relocates a module in main memory, then
copies its bytecode to aux with the ROM's XMOVE (`call($C311, ...)`,
with $3C, $3E and $42 set, cmdjit.pla:1229-1235) and frees it in
main. Data, strings and the def table stay in main. XLOAD does the
same:

- **Move after the fixups,** which patch bytecode words too, and
  before the hand-off. Then the heap goes back to the body's bytecode
  offset (the header's), not its end: the program needs about 25K
  less main memory than on the 64K machine (cccomp's bytecode).
- **Find free aux memory without `xheap`,** CMDJIT's aux heap pointer
  (from $0200, below `xheaptop`, $A000; the JIT's compiled code goes
  above), which CMDSYS doesn't export. execmod saves and restores it
  around a program, so XLOAD needn't move it, only avoid what's there:
  FILEIO's bytecode and its own, XLOAD's being the last loaded. The
  highest address in XLOAD's own def entries (scanned from 4.2's first
  entry to the zero byte that ends the table), plus init's size, is
  where free aux starts. Aux has about 37K free ($0C00-$A000, if the
  text pages are reserved), so cccomp's 25K fits with room.
- **The second part's bytecode** goes in aux too, above where the
  program's will go.

### 4.5 The hand-off

XLOAD can't stay resident (R4), and the program can't return into
XLOAD's code (its heap tables are allocated over it). So the last step
is a tail call: the program's init returns straight into loadmod, as
if loadmod had called it.

**The 64K VM's calling convention** (plvm02.s, read 2026-11-11):

- **The Apple's zero page** (vmsrc/apple/plvmzp.inc, the same for the
  128K VM): the evaluation stack's high bytes `ESTKH` at $A0-$BF and
  low bytes `ESTKL` at $C0-$DF, indexed by the 6502's X, which counts
  down as values are pushed; `IFP`, the frame pointer, at $E0-$E1;
  `JMPTMP` at $E6 (a JMP opcode) with `TMP` at $E7-$E8; the IP at
  $F2-$F3.
- **CALL and ICAL** (:1380-1423) push the return IP (2 bytes) on the
  6502 stack, then `JSR JMPTMP` (2 more). A def entry's `JSR $03D6`
  pushes 2 more, which IINTRP (:163-174) pulls at once. So **a call
  holds 4 bytes of the 6502 stack**, and nothing else.
- **ENTER** (:1425-1447) subtracts the frame size from `IFP`;
  **LEAVE** (:1450-1457) adds it back and does an RTS; **RET** is a
  bare RTS (:1458). `IFP` isn't saved anywhere: each function
  restores it itself.
- **loadmod calls init** with ICAL through a new def entry (cmd.pla:
  1166). When init's first op runs, the 6502 stack holds loadmod's 4
  bytes on top, `IFP` is loadmod's, and X is loadmod's evaluation
  stack index.
- **ccrel's init has no frame** and calls `main` directly when main
  takes no parameters (link.c, init_code), with nothing left on the
  evaluation stack. So **XLOAD's `main` takes no parameters** and
  reads `cmdsys.cmdline` itself: the startup `__start` would split the
  command line in place.

**The capture.** The first part's `int main(void)` has no locals, so
no frame. Its first act is to call a native **capture routine**,
through a C function pointer to a data array (ccish emits ICAL for
that; CMDSYS's `call` isn't used, since it switches in the ROM and
pushes more onto the stack, cmd.pla:176-213). At its first
instruction the 6502 stack pointer is init's less 8 (init's call of
main, main's call of the routine), `IFP` is loadmod's, and X is
loadmod's evaluation stack index. It returns the stack pointer and X
in one word (14 bytes, zero page only, so it runs anywhere):

```
86 E7     STX TMPL        ; keep the evaluation stack index
BA        TSX             ; the 6502 stack pointer
8A        TXA
A6 E7     LDX TMPL
CA        DEX             ; push the result:
95 A0     STA ESTKH,X     ;   high byte, the stack pointer
A5 E7     LDA TMPL
95 C0     STA ESTKL,X     ;   low byte, the evaluation stack index
60        RTS
```

main also reads `IFP` from $E0 in C.

**The hand-off routine.** The second part writes those values (the
stack pointer plus 8), and the address of the program's init def
entry, into a 14-byte routine and calls it the same way:

```
A9 ll     LDA #<IFP
85 E0     STA IFPL
A9 hh     LDA #>IFP
85 E1     STA IFPH
A2 ss     LDX #SP         ; init's stack pointer:
9A        TXS             ;   loadmod's 4 bytes on top again
A2 xx     LDX #X          ; loadmod's evaluation stack index
4C ll hh  JMP init        ; the program's init def entry
```

The program's init then runs exactly as if loadmod had called it, and
its RET returns into loadmod's ICAL. loadmod frees XLOAD's (stale)
init address, which execmod's restore of the heap overrides, and
execmod drops XLOAD's symbol-table entries as usual. loadmod's frame,
and the 6502 stack above the restored pointer, were never touched.

**On plvm** the VM is C, with its stacks in C variables, so neither
routine can run there. XLOAD ends with a plain call of the program's
init instead, which still tests everything else.

**On the 128K VM** the routines need adjusting, after a reading of
plvmjit02.s's calls and returns: its indirect entry into aux bytecode
(IINTRPX, :241-257) pushes the status register (`PHP`, `SEI`, then
`STA ALTRDON`), so a call into aux code holds 5 bytes, not 4, and runs
with interrupts off and aux memory read. The capture's offset is
then 9, not 8, if init and main run from aux; and the hand-off must
leave the state that init's return into CMDJIT's loadmod expects.

## 5. Key factors

What shapes the implementation, most of it measured or read in this
tree (v2.21):

- **Memory, 64K Apple.** 41,171 bytes free at the PLASMA 2.20 prompt
  (`MEM FREE:$A0D3`, MAME's 64K IIe). cccomp needs 40,191 running, a
  lower bound (doc/measure.txt), and about 310 more for CMD's frames,
  which stay on the stack while a program runs (options, fact 11):
  about 670 to spare. At XLOAD's load peak: the def table (1,460) and
  body (31,500), the second part (about 2K, a guess), FILEIO (1,998),
  a 512-byte buffer, and frames (about 400): **about 37.9K, leaving
  about 3.3K**.
- **There's no other memory.** The heap runs from CMD's end to the
  frame stack; the low pages are all in use (options, section 2 and
  correction 1-3); hi-res page 1 is part of the heap.
- **loadmod reads a module's file to its end** (options, fact 12): the
  image must be a separate file.
- **What CMDSYS exports** (options, fact 14): the heap functions,
  `memcpy`, `memset`, `call`, `lookupsym`, `jitcount`, `jitsize`, the
  command line, and the rest; CMDSYS's names are in the symbol table,
  so `lookupsym` finds them. Not exported: reloc, lookupdef, adddef,
  addsym, loadmod, and the 128K VM's `xheap`. So XLOAD reimplements
  only adddef's few lines and the fixup loop.
- **File I/O through FILEIO.** Every program ccish builds depends on
  it, so it's already in memory and costs XLOAD nothing; it's the same
  interface on the Apple (libsrc/apple/fileio.pla) and plvm
  (vmsrc/sysio.c). Each file beyond the first takes a 1K page-aligned
  buffer from the heap (options, fact 13).
- **The VMs differ** in def entries (4.3), where bytecode lives (4.4),
  and the 6502 stack per call (4.5); they share the bytecode and, on
  the Apple, the zero page.
- **The C64** has a partial PLASMA port outside ccish; leave all of it
  alone (the user, 2026-11-12), including the untracked
  src/vmsrc/plvmzp.inc, which is the C64's. A C64 row in 4.2's table
  would be possible later.

## 6. Sizes on the disk

From ccrel -x (2026-11-12, the small build's objects); XLOAD itself
perhaps 3K, once:

| Program | Body | Data segment (as runs) | Functions | Fixups (words) | Image file | REL module |
|---|---:|---:|---:|---:|---:|---:|
| ccpp | 19,967 | 5,129 (1,362) | 167 | 1,837 (1,757) | 18,478 | 27,814 |
| cccomp | 31,500 | 4,774 (433) | 292 | 2,833 (2,745) | 30,683 | 43,799 |
| ccgen | 27,846 | 4,622 (1,676) | 206 | 2,160 (2,041) | 27,579 | 36,985 |
| ccrel | 12,736 | 2,343 (466) | 98 | 1,164 (1,110) | 12,326 | 17,719 |

The four images take 89,066 bytes of the disk, against 126,317 as REL
modules.

`-x` cost ccrel 951 bytes of code and 640 of heap (the function
table's offsets), leaving it about 13.4K to spare running.

## 7. Open questions, and what to check

Still to learn:

1. The ProDOS file type for images.
2. Whether FILEIO's init allocates anything on the heap (it would sit
   between CMD and XLOAD).
3. How small the first part can be, and the second part's real size.
4. Which disk is the target: the 41,171 is from the 2.20 disk; this
   tree is 2.21, whose CMD is 745 bytes smaller.
5. How `lookupsym` reports a missing name (0, presumably), so XLOAD
   can say which import or dependency is missing.
6. For the 128K VM: what else may sit in aux memory (the 80-column
   text page, the `resxtxt` flags, cmdjit.pla:968-1003); its calls and
   returns, for the hand-off (4.5); whether XMOVE is safe to call
   from code that's itself running from aux (CMDJIT calls it from
   main-memory code).
7. Whether CMDJIT has enough free main memory to load cccomp as a
   plain REL module (its loader still reads the whole file into main
   memory first). Not needed for R2, but worth knowing.

To check once XLOAD runs, on MAME and then a real Apple II:

- The capture routine's results, against 4.5's arithmetic.
- That the first part's `main` has no frame and calls the capture
  routine with an empty evaluation stack (a listing test).
- That the program's init returns into loadmod, and execmod puts back
  the heap and symbol table: `MEM FREE` after a run equals the one
  before.
- That an error in the program (init's negative return) is reported
  as CMD reports one today (`ERR:$xx`).
- That every file XLOAD opened is closed before the `heaprelease`.
- The load time against today's, for ccgen (which loads either way).
- On a 128K machine with PLVM.128: the same image loads, its bytecode
  goes to aux, the JIT compiles its hot functions, and `MEM FREE` is
  the same after the run.

## 8. Plan

1. ~~The hand-off, read from plvm02.s~~ (4.5), 2026-11-11.
2. ~~ccrel's `-x` and the image format~~ (section 3), 2026-11-12,
   checked by tools/xlcheck.py; version 2 the same day, for R2, and
   version 3, for the data segment's zero runs.
3. Write XLOAD in ccish C: the first part, and the second part as an
   image inside it, for the 64K VM and plvm, with 4.2's test refusing
   the rest.
4. Test under plvm with ccish's suite (a plain call in place of the
   hand-off), then on MAME with tools/a2screen (with its guards),
   booting a disk with XLOAD and cccomp's image.
5. The 128K VM (4.3-4.5): read plvmjit02.s's calls and returns for the
   hand-off, then test on MAME's 128K IIe with PLVM.128.
6. Then the other three programs, if the disk savings are worth it.
7. Separately, offer upstream a streaming loader in CMD for everyone
   else's large modules (options, option 1), with fixes for loadmod's
   two latent bugs (options, fact 5).
