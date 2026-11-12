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

**Status (2026-11-12).** XLOAD is written (xload/: xload.c, the first
part; xload2.c, the second; xlcore.c, what they share) and works under
plvm, for the 64K Apple VM and plvm, with `-V` diagnostics (4.6).
ccrel and ccish write version 4 images with `-x` (section 3). Under
plvm, `make test` runs every run test again as an image through XLOAD
(the img tests), XLOAD's own test program and its errors
(tests/special/xload.sh), and ccish's four programs as images, which
must write the same files, byte for byte, as the REL programs
(tests/special/xload_self.sh). Nothing has run on an Apple II yet,
so the native hand-off (4.5) is untested: plan step 9, MAME, is next.

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
- R6. **Eventually, every platform PLASMA supports**, to the extent
  that's reasonable (the user, 2026-11-12): in this tree, the Apple 1
  with CFFA1 (plvm01.s, a1cmd.pla), the 64K Apple II (plvm02.s), the
  128K Apple II (plvmjit02.s), the 65802/65816 Apple II (plvm802.s),
  the Apple /// under SOS (plvm03.s, plvmjit03.s, soscmd.pla), the C64
  (vmsrc/c64, partial), and plvm on the host. The image format is
  VM-neutral, and what's machine-specific in XLOAD is kept to a few
  places (4.2's table, 4.3's def entries, 4.4's memory model, 4.5's
  hand-off), so that each platform is mostly a new row. Order: plvm
  and the 64K Apple II first, then the 128K, then the rest.

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

## 3. The image format (version 4)

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
  byte per fixup instead of four (cccomp: 2,745 words in 2,833 bytes).
  A REL entry is always a type byte, a full 16-bit address, and a byte
  that only an import uses, since PLASMA's loaders take entries in any
  order. The image's fixups come in address order (ccgen writes them
  so, at no cost), so each gives only the gap from the word before,
  which in code with a relocated word every ten bytes or so nearly
  always fits in 6 bits; the other 2 bits give its kind, of the three
  an image needs; and only imports (51 of cccomp's 2,745) add an index
  byte. Also a 2-byte table entry per function instead of a 4-byte RLD
  record; and no bytes at all for zero-filled
  data, which goes in a zero region of which the file gives only the
  size (the traditional `.bss`; this does PLASMA request 12,
  doc/plasma-requests.md, for images). cccomp's image file was 30,683
  bytes in version 3, against its 43,799-byte REL module; version 4's
  is still to measure.
- **It's faster.** A REL module moves its large zero-filled globals to
  the heap to keep its file small, at the cost of an LAW of a pointer
  word for each access; an image keeps every one in its zero region,
  reached with an LA, about 24 cycles less on the 6502, in the same
  memory.
- **It holds only what ccish's programs use.** No exports (ccish
  modules export nothing), no 8-bit fixups, and one kind of body.

### 3.2 The layout

ccrel writes it with `-x` (link.c; the `ccish` driver passes `-x` to
the same code). Words are little-endian, and every address in it is
an offset from the body's start, where XLOAD loads it. The body is
three parts, in this order:

1. **The initialized data:** every object's data, then the strings.
2. **The zero region:** every zero-filled global (section 3.3), of any
   size. Not in the file.
3. **The bytecode:** every function, the helpers, and init last.

| Bytes | What |
|---|---|
| 2 | Magic: `X` `L` ($58 $4C) |
| 1 | Version: 4 |
| 1 | Flags: 0 (none yet; a "no JIT" bit, as a REL header's, could go here) |
| 2 | The initialized data's length, D |
| 2 | The zero region's length, Z: it's at D |
| 2 | The bytecode's length, C: it's at D + Z |
| 2 | The functions, init's included (the last) |
| any | Dependencies: module names in DCI form, as a REL module's, then $00 |
| any | Imports: names in DCI form, numbered from 0, then $00 |
| 2 each | The function table: each function's offset, in address order, init last |
| D | The initialized data |
| C | The bytecode |
| any | The fixups, then $FF |

- **References to functions** (a call, or a function pointer in code
  or data) hold the function's number in the function table. **The
  image holds no def entries:** XLOAD builds them, in the form the VM
  it's running on wants (section 4.3), and each such word becomes its
  entry's address. So one image serves every VM whose bytecode is the
  same (R2).
- **The zero region is cleared by the program's init,** with CMDSYS's
  `memset`, before it calls `main`, as C's startup code clears `.bss`.
  Not by XLOAD, which uses the region for its own second part while it
  loads (4.1). It comes before the bytecode so that, on the 128K Apple,
  where the bytecode moves to aux memory, data and zeros stay one block
  in main memory.
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
  init's (none falls in the zero region). They come out of ccgen in
  order with no sorting; ccrel stops with an error if they ever don't.
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
- **Possible later:** the "no JIT" flag.

### 3.3 What goes in the zero region

Measured over ccish's own sources and library (2026-11-12, an
instrumented scratch cccomp), the zeros in the data segment are:

| From | Bytes | Objects |
|---|---:|---:|
| Globals with no initializer, under `BSS_MIN` (256 bytes) | 9,432 | 333 |
| Static locals with no initializer | 1,799 | 14 |
| Zero tails inside initializers (`int t[8] = {1};`) | 168 | 2 |

plus the large zero-filled globals (`BSS_MIN` or more), which are
already zero-filled symbols, and explicit zero initializers (`int x =
0;`, `= {0}`), not counted. So the zero region takes:

- **every global and static local with no initializer, of any size**
  (plan step 3: cccomp puts them in the file's zero-filled space,
  where before only arrays and structs of `BSS_MIN` bytes or more
  went; static locals go in each object's zero area, plan step 3);
- **every one whose initializer is all zeros** (plan step 4, done), as gcc
  does with `.bss`; C can't tell the difference (C11 6.7.9p10).

What's left as zeros in the initialized data is only the zeros inside
initializers that aren't all zero: 168 bytes in all of ccish, not
worth an encoding. (Version 3 stored the data as runs of literal bytes
and zeros; version 4 drops that.)

A REL module still moves its zero-filled globals of `BSS_MIN` or more
to the heap, and only those: cccomp's `movable()` (parse.c), which
makes their accesses take the one form ccrel can patch into an LAW,
gets the same size test, so a small one keeps its ordinary code. The
rest go in ccrel's block of zeros after every object's data, as large
ones that stay do now, so a REL module's file keeps its size.

### 3.4 Earlier versions, and checking

All 2026-11-12, never used by any loader: version 1 (bc34918a) put a
table of 5-byte `JSR $03D6` def entries in the image, which fit only
the 64K VM; version 2 (8e3006d3) replaced it with the function table,
for R2; version 3 (81251c4a) stored the data segment as runs of
literal bytes and zeros; version 4 replaces the runs with the zero
region.

**Checking it.** Versions 1 to 3 were checked by tools/xlcheck.py
(now gone), which compared each image with the REL module of the same
link. Version 4's layout and accesses differ from the REL module's by
design (LA, not LAW; no pointer words; init clears the zero region),
so it landed with XLOAD, and images are checked by running them
through XLOAD (section 8, steps 6-8), and by tools/xlinfo.py, an
independent reader of the format, against XLOAD's `-V`.

## 4. What XLOAD must do

XLOAD's file is an ordinary $6502 module in two parts:

1. **The first part** (xload.c) is all loadmod runs: a relocator for
   the second part, plus what must happen before it (4.2, 4.5). Its
   module takes 4,871 bytes of memory, 2,452 of them the second part.
2. **The second part** (xload2.c), the real loader, is carried inside
   the first part's data as an image in section 3's format
   (tools/img2h.py makes it a C array, xl2img.h). loadmod sees only
   bytes. Once relocated, it's ordinary ccish C: globals, strings,
   helpers and calls all work. Placed, it takes 2,933 bytes (18
   functions, 409 of data, 807 of zero region, 1,627 of code).
3. **xlcore.c,** included by both, reads an image from a byte source
   (memory for the first part, the file for the second), builds its
   def entries, and applies its fixups.

### 4.1 The steps

1. loadmod loads XLOAD (and FILEIO, its dependency) and calls its
   init, which calls the first part's `main` directly.
2. The first part tells which VM it's on (4.2), captures the VM's
   state for the hand-off on the Apple (4.5), takes `XLOAD` and any
   `-V` off the command line, and opens the program to read its
   header (12 bytes) for its sizes.
3. It places the second part (below), builds its def entries (4.3),
   applies its fixups, looks up its imports with `lookupsym`, clears
   its zero region, and calls it at its first function (`xl2_entry()`,
   the first in xload2.c, so number 0 in its function table), with a
   struct in its own frame (`struct xl_args`, xlcore.c) that says the
   VM, the def entries to build, where the program goes, its file, -V,
   and the captured state. On the 128K VM it would also move the second
   part's bytecode to aux (4.4).
4. The second part checks the dependencies, looks up the imports, and
   checks that the program fits: its body (D + Z + C), its def table,
   and 512 bytes for its frames (as `HEAP_SPARE` does today), below
   the frame stack; if not, it prints "not enough memory" and fails,
   as a REL program's init would.
5. It reads the program's initialized data and bytecode straight to
   their final places: the bottom of the heap, where XLOAD's own module
   starts, over the first part, leaving the zero region's Z bytes
   between them unwritten. It reads in chunks through a small buffer,
   then streams the fixups, applying each as it comes.
6. It builds the program's def entries (4.3), and on the 128K VM moves
   its bytecode to aux (4.4).
7. It closes its files and sets the heap to the end of the program's
   memory (`heaprelease`): the body's end, or on the 128K VM, the zero
   region's end (D + Z). Nothing may allocate after this, since the
   second part is in freed memory.
8. The hand-off (4.5) jumps to the program's init, which clears the
   zero region (over the second part, if it was there) and calls
   `main`. Nothing of XLOAD stays (R4). Under plvm, the second part
   returns the init entry's address to the first part, which calls it
   (4.5).

**Errors** before step 5 return to the first part, which returns an
error status (1; 2 for the usage). After step 5 has begun, on the
Apple, the first part is gone: the second part hands off instead to a
stub init (a def entry and `MINUS1 RET`, in its data), so that CMD
reports an error as for any program.

**Why load in place:** the alternative, reading and fixing up the
program higher up and block-moving it down, would add about 16 cycles
a byte, about half a second for cccomp's 31.5K at 1 MHz. "Apples
aren't fast." Loading in place avoids it, and keeps the hand-off to
restoring state and a jump.

**Where the second part goes:** inside the program's zero region, if
it fits there (its image, its own zero region, and its read buffer);
otherwise just above the program's end. Either way its frames grow
from the current frame pointer as usual, and it's in memory the
program will clear or allocate over. Inside the zero region, loading
needs no memory beyond the program's own, so the load peak is lower
than the running need. It must also stay clear of the first part,
which is running while it's copied: so it goes at the higher of the
zero region's start and the first part's end (the heap's mark). The
zero regions (section 6): ccpp 16,914, ccrel 12,226, ccgen 7,490,
cccomp 6,588 bytes, against the second part's 2,933. For cccomp on the
Apple, the region runs from 2,045 to 8,633 bytes above the heap's
bottom, and the first part ends at 4,871, so the second part ends at
7,804: inside, with 829 to spare. Under plvm, whose loader keeps a
module's bytecode out of 6502 memory, the first part ends lower. On
the 128K VM the second part's bytecode would go to aux (4.4), and only
its data need a place.

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
| anything else | | refuse, with a message, for now (R6) |

A listing test can pin that the two entries are consecutive.

**For R6, still to read:** the def entries and memory models of the
65802/65816 VM (plvm802.s), the Apple /// (plvm03.s, soscmd.pla,
plvmjit03.s, soscmdjit.pla, which uses SOS, not ProDOS), the Apple 1
(plvm01.s, a1cmd.pla), and the C64 (vmsrc/c64). Each needs a row
here and in 4.3, and its own answers to 4.4 (where bytecode lives)
and 4.5 (the hand-off), and a FILEIO (libsrc/c64/fileio.pla exists;
the Apple 1's and Apple ///'s are to check). Reading the C64 port is
fine; changing it isn't (the user, 2026-11-12: leave the C64 work
alone).

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
init instead, which still tests everything else. **The first part
makes that call,** not the second: the second part returns the init
entry's address to it. The second part is an image in 6502 memory,
where the program's zero region or heap may overwrite it, so the call
would return into whatever overwrote it. The first part is a REL
module, whose bytecode plvm keeps in host memory, out of the
program's reach. Its globals aren't (they're at the heap's bottom,
under the program), so after the second part returns it uses only
its locals.

**Also on plvm:** FILEIO's GETFILEINFO prints "unimplemented!"
(vmsrc/sysio.c), so XLOAD skips the ProDOS file type check there.

**On the 128K VM** the routines need adjusting, after a reading of
plvmjit02.s's calls and returns: its indirect entry into aux bytecode
(IINTRPX, :241-257) pushes the status register (`PHP`, `SEI`, then
`STA ALTRDON`), so a call into aux code holds 5 bytes, not 4, and runs
with interrupts off and aux memory read. The capture's offset is
then 9, not 8, if init and main run from aux; and the hand-off must
leave the state that init's return into CMDJIT's loadmod expects.

### 4.6 Diagnostics

`+XLOAD -V PROGRAM ARGS` prints what XLOAD finds and does, so that
the tests (and anyone on an Apple II) can see that it's doing the
right thing, not only that the program happened to run. For
tests/run/hello.c under plvm:

```
XLOAD: import PUTLN $03CC                 (one line per import)
XLOAD: VM plvm, entries JSR $FF08
XLOAD: data 7, zero 0, code 35, functions 3, imports 3
XLOAD: defs $04D8, body $04E7, zero $04EE, code $04EE, end $0511; XLOAD2 $1274
XLOAD: fixups 6: address 1, function 2, import 3
XLOAD: init $04E2, free $F9A2; a plain call
```

The VM (4.2) and the def entries it builds (4.3); the image's sizes;
where each part of the program went, and the second part; the fixups
applied, of each kind; the init entry it calls, the memory left free
above the program, and on the Apple the state captured for the
hand-off (`hand-off SP $xx, X $xx, IFP $xxxx`, 4.5) in place of "a
plain call". tests/special/xload.sh checks the sizes, fixups and
imports against tools/xlinfo.py's reading of the image, and that the
places add up.

Without `-V` it prints nothing but errors, each naming what's wrong:
`can't open NAME`, `not an XLOAD image, version 4`, `needs the module
NAME`, `can't import NAME`, `not enough memory`, `the image is cut
short`, `a bad fixup`, `a function outside the image`, `doesn't know
this VM yet`, and the usage.

## 5. Key factors

What shapes the implementation, most of it measured or read in this
tree (v2.21):

- **Memory, 64K Apple.** 41,171 bytes free at the PLASMA 2.20 prompt
  (`MEM FREE:$A0D3`, MAME's 64K IIe). cccomp needs 40,191 running, a
  lower bound (doc/measure.txt), and about 310 more for CMD's frames,
  which stay on the stack while a program runs (options, fact 11):
  about 670 to spare. Loading, with the second part in the zero region
  (4.1), needs no more than the program's own body and def table,
  FILEIO, and frames: under the running need. With the second part
  above the body instead, the load peak would be about 40.3K, so the
  zero region matters.
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
  src/vmsrc/plvmzp.inc, which is the C64's. XLOAD should support it
  eventually (R6), as a row in 4.2's table.

## 6. Sizes on the disk

Version 4 images (ccrel -x, 2026-11-12, the small build's objects, as
measure.py builds them):

| Program | Data | Zero region | Code | Functions | Fixups (bytes, words) | Image file | REL module |
|---|---:|---:|---:|---:|---:|---:|---:|
| ccpp | 1,450 | 16,914 | 14,728 | 167 | 1,743 | 18,422 | 27,814 |
| cccomp | 565 | 6,588 | 26,856 | 296 | 2,767 | 30,957 | 44,159 |
| ccgen | 1,921 | 7,490 | 23,159 | 206 | 2,028 | 27,728 | 37,075 |
| ccrel | 580 | 12,226 | 10,485 | 98 | 1,135 | 12,538 | 18,156 |

The four images take 89,645 bytes of the disk, against 127,204 as REL
modules, and XLOAD 6,010 bytes, once: about 31.5K less. The images'
code differs from the REL modules' Code column a little: it includes
init, which clears the zero region instead of allocating moved
globals, and every zero-filled global is reached with an LA, never an
LAW of a pointer word. cccomp's 296 functions are against `MAX_DEFS`,
320.

**XLOAD itself:** the first part's module takes 5,128 bytes of memory
(2,709 of them the second part's image); the second part, placed,
3,136 (18 functions, 483 of data, 807 of zero region, 1,756 of code).
For cccomp on the Apple, the second part lands inside its zero region
(2,045 to 8,633 bytes above the heap's bottom), from 5,128 to 8,264:
**only 369 bytes to spare**. If XLOAD grows past that, the second part
goes above cccomp instead, and the load peak rises to about 41K, about
all of the 41,171 free. Ways to win room back, if needed: a smaller
read buffer (512 bytes now, 256 would do), or the second part's image
in a file of its own (XLOAD2), so that it isn't in the first part's
module (about 2.7K lower).

`-x` cost ccrel 951 bytes of code and 640 of heap (the function
table's offsets), leaving it about 13.4K to spare running.

## 7. Open questions, and what to check

Still to learn:

1. The ProDOS file type for images.
2. Whether FILEIO's init allocates anything on the heap (it would sit
   between CMD and XLOAD).
3. ~~The parts' real sizes~~: section 6.
4. Which disk is the target: the 41,171 is from the 2.20 disk; this
   tree is 2.21, whose CMD is 745 bytes smaller.
5. ~~How `lookupsym` reports a missing name~~: 0, under plvm (tested);
   XLOAD doesn't yet check the image's ProDOS file type (2.).
6. For the 128K VM: what else may sit in aux memory (the 80-column
   text page, the `resxtxt` flags, cmdjit.pla:968-1003); its calls and
   returns, for the hand-off (4.5); whether XMOVE is safe to call
   from code that's itself running from aux (CMDJIT calls it from
   main-memory code).
7. Whether CMDJIT has enough free main memory to load cccomp as a
   plain REL module (its loader still reads the whole file into main
   memory first). Not needed for R2, but worth knowing.
8. For R6: each other platform's def entries, memory model, hand-off
   and FILEIO (4.2).

To check once XLOAD runs, on MAME and then a real Apple II:

- The capture routine's results, against 4.5's arithmetic.
- ~~That the first part's `main` has no frame and calls the capture
  routine with an empty evaluation stack~~: a listing check in
  tests/special/xload.sh.
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
   versions 1-3, checked by tools/xlcheck.py; version 4 specified.
3. ~~cccomp: no initializer, any size, into the zero-filled space~~
   (3.3), 2026-11-12, for globals: `movable()` stays limited to
   `BSS_MIN` and up, ccrel moves only those, and the peephole stage
   folds constants into any symbol's address, so the code is the same
   (the gen tests' ops are unchanged, only their operands' names).
   **Static locals** came next (2026-11-12): a zero-filled global is
   reached through its symbol, but cccomp writes the symbol summary
   when the file ends, by which time a static local's variable slot
   has gone with its block. So static locals with no initializer have
   no symbols: they're in each object's **zero area**, at offsets, as
   initialized data is in its data (object version 9, IR version 17:
   relocation kind 6; a global op's symbol operand -2 in the IR;
   ccgen's `FIX_ZERO`, folded as `FIX_DATA` is). ccrel puts each
   object's zero area first in its share of the zero-filled globals
   that stay. Cost: 74 bytes of cccomp's code (891 to spare running).
   Tests: tests/run/static_zero.c, tests/link/static_zero,
   tests/errors/init_range_zero.c.
4. ~~cccomp: all-zero initializers too~~ (3.3), 2026-11-12: `int x =
   0;`, `= {0}` and the like go in the zero-filled space (a static
   local in the zero area). irout.c's data writer holds zeros back
   until a nonzero byte or an address comes (then writes them as a Z
   record, or bytes if fewer than 4), so that the parser can take back
   an initializer of only zeros (`data_drop_zeros()`). It costs
   cccomp 132 bytes of code (744 to spare running) and saves nothing
   in ccish's own programs, whose sources have no such initializers;
   it's for the programs ccish compiles. Tests:
   tests/run/zero_init.c, tests/run/zero_init_place.c.
5. ~~ccrel's version 4, and XLOAD, together~~ (done 2026-11-12; 3.4 says why): under
   `-x`, nothing moves to the heap; the zero region; init clears it.
   XLOAD in ccish C, the first part and the second part as an image
   inside it, for plvm and the 64K VM, refusing the rest (4.2), with
   `-V` (4.6).
6. ~~A small test program first~~ (done: tests/xload/xltest.c, tests/special/xload.sh), before any of ccish's own: run
   under plvm through XLOAD with `-V`, its output and status checked,
   and its diagnostics checked against what ccrel's listing says (the
   VM, sizes, where things went, the fixup counts, the imports). It
   uses everything an image can hold: initialized data with addresses
   (of data, strings and functions) in it, a zero region (small and
   large globals, a static local, read before written, to see they're
   zero), calls through function pointers, helpers, CMDSYS and FILEIO
   imports, `argv`, and an exit status. Plus error cases: a missing
   image, a bad magic or version, a missing dependency.
7. ~~Then ccish's run tests as images~~ (done: the img tests, tests/imgrun.sh) under plvm through XLOAD, with
   their expected outputs and statuses (in `make test`); xlcheck.py
   goes.
8. ~~Then ccish's four programs as images~~ (done: tests/special/xload_self.sh): compile with them under
   plvm through XLOAD, and require the same output, byte for byte, as
   the REL programs' (a bootstrap-like check).
9. **MAME, the 64K Apple II,** with tools/a2screen (with its guards):
   the small test program first, then cccomp.
10. **The 128K VM** (4.3-4.5): read plvmjit02.s's calls and returns for
    the hand-off, then test on MAME's 128K IIe with PLVM.128.
11. **The other platforms** (R6), as far as reasonable.
12. Separately, offer upstream a streaming loader in CMD for everyone
    else's large modules (options, option 1), with fixes for loadmod's
    two latent bugs (options, fact 5).
