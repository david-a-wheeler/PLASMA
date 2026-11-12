# Loading cccomp on a 64K Apple II: options, and XLOAD

cccomp, ccish's front end, can't load on a 64K Apple II. It would fit
while running, but every PLASMA loader reads the whole module file,
relocation data included, before anything runs, and the whole file
doesn't fit. This document records how the 64K system loads a module,
the options for getting past that, and the one we chose: **XLOAD**, a
second-stage loader that ccish ships, so that PLASMA's own loader
needn't change.

**Status (2026-11-11).** Decided in discussion: XLOAD (section 5), on
the `c-compiler` branch with the rest of ccish (no separate branch).
Not yet implemented. **Still to come, first: reading plvm02.s** for
the VM's state when a module's init is called (the 6502 stack, the
evaluation stack index in X, and the frame pointer `IFP`), which
XLOAD's hand-off depends on (section 5.4); and its ENTER handler, for
any per-frame overhead in the loader-frame figure of fact 11.

The first version of this document (eef1dd1e) had errors in its
memory map and missed options; section 8 lists the corrections, so
nothing from it is lost.

## 1. The problem, in numbers

From doc/measure.txt (the summary at its end, 2026-11-11), against the
41,171 bytes free at the PLASMA 2.20 prompt (`MEM FREE:$A0D3`,
measured on MAME's 64K enhanced IIe, 2026-11-09; approach.md, "Free
memory, measured"):

| cccomp | Need | Spare |
|---|---:|---:|
| Loading today (the whole file) | 47,233 | -6,062 |
| Loading, the RLD streamed | 34,959 | 6,212 |
| Running (a lower bound) | 40,191 | 980 |

Running, the real margin is smaller: about **670 bytes**, since CMD's
own frames (about 310 bytes) stay on the frame stack while cccomp
runs and aren't in measure.txt (fact 11). So loading is the blocker,
and running is the tight figure.

The maintainer's reply on https://github.com/dschmenk/PLASMA/issues/78
("So this would be a pretty significant update", and whether a 128K
machine would be the baseline; dschmenk, 2026-11-11) is one reason to
prefer an option that needs no change to PLASMA. The answer on the
record: ccish already runs and bootstraps under the portable VM, and
the goal is a 64K machine (david-a-wheeler, 2026-11-11, with the fit
table). The draft issue is ,streaming.md at the repository root.

## 2. How the 64K system loads a module

Read from this tree's sources (v2.21): src/vmsrc/apple/plvm02.s,
cmdstub.s and cmd.pla, and src/libsrc/apple/fileio.pla.

### Memory map

| Where | What |
|---|---|
| $0000-$01FF | Zero page (the VM's registers and evaluation stack) and the 6502 stack |
| $0200-$02FF | The input line buffer (CMD's `getlnbuf` is $01FF; `execsys` uses $0280) |
| $0300-$03FF | `STRBUF` at $0300 (the prefix and system path), and the VM's entry vectors at $03D0 (direct) and $03D6 (indirect) (plvm02.s:41-42, :296-301) |
| $0400-$07FF | **The 40-column text screen**, page 1 |
| $0800-$0BFF | ProDOS's 1K I/O buffer for the first open file (`iobuffer`, cmd.pla:4): CMD's own reads, then a program's first file |
| $0C00 to about $1B45 | CMD (bytecode, run by the VM): the command loop, the loader, the heap allocator, and the CMDSYS functions programs import. `_CMDEND` is $1B45 in this tree's build |
| From `@lastdef` up | The heap: modules, their def tables, their heap data (cmd.pla:1426, `heap = @lastdef`) |
| Down from $BF00 | The frame stack (cmdstub.s sets `IFP` to $BF00) |
| $BF00-$BFFF | ProDOS's global page |
| Language card bank 2, $D000 up | The VM, copied there at boot (plvm02.s:65-83); its BYE handler sits at $D100 |
| Language card, $DA00 | The symbol table (cmd.pla:5): module names, CMDSYS's exports, each module's exports |
| The rest of the language card | ProDOS (general Apple II knowledge, not checked here) |

Not checked: where the VM ends. The committed plvm02.sym says
`VMEND = $DB2D`, past the symbol table's $DA00, but that file is from
2023-12-15 and plvm02.s changed on 2024-01-03, so it's stale; acme
0.97 here can't assemble plvm02.s (section 7) to settle it.

### Boot

1. ProDOS runs the PLASMA system file at $2000. It copies the VM into
   language-card bank 2 ($D000-$DFFF; plvm02.s:65-83), with its BYE
   handler at $D100, the page from which ProDOS runs BYE (QUIT) code,
   so BYE reloads CMD (plvm02.s:84-90, :176-253).
2. It installs the zero-page fetch loop and the page-3 vectors, then
   reads the file `CMD` into $2000 through the buffer at $0800
   (plvm02.s:226-253, READPARMS :268-272), and jumps there.
3. cmdstub.s copies CMD down to $0C00. Its loop copies 17 pages
   ($0C00-$1CFF: X counts down from $10 and stops when it goes
   negative), sets `IFP` to $BF00, and jumps to $0C00.
4. CMD sets `heap = @lastdef`, the end of its own image, enters
   CMDSYS's exports in the symbol table, and prints `HEAP START` and
   `MEM FREE` (cmd.pla:1442-1468). `MEM FREE` is `availheap`, the
   frame pointer less the heap, measured from CMD's top level: with no
   loader frames on the stack (fact 11).

### loadmod (cmd.pla:1005-1180), run by `+NAME` through execmod

1. Open the file (through $0800), check that its ProDOS type is $FE,
   and read its first 128 bytes into `header`, a local array on the
   frame stack.
2. Load each dependency not already in the symbol table: close the
   file, call loadmod recursively (FILEIO, for ccish), reopen it, and
   re-read the header.
3. Allocate the def table on the heap, `defcnt * 5 + 1` bytes (the
   header's count, init's entry included).
4. Allocate the body on the heap: the module's size less its header
   and dependency list. Copy in the part already read.
5. Read the rest of the file, the rest of the body and then the RLD
   and ESD, in 4K reads until EOF, into the memory after the body
   (:1083-1088). The RLD and ESD get no heap allocation, and nothing
   checks that they fit below the frame stack.
6. Add the module's name to the symbol table.
7. For each $02 entry, add a 5-byte def entry: `JSR $03D6` and the
   function's address. Calls go through these.
8. Apply each fixup: an internal one adds the load offset, or gives
   the def entry's address if the target is in the bytecode; an
   external one adds the address of the ESD name's symbol.
9. Add the ESD's exports to the symbol table.
10. `releaseheap(modend)`: the RLD and ESD go.
11. Call init through a new def entry. If it returns below
    `modinitkeep`, free init's code (the tail of the bytecode).

execmod (:1329-1350) then puts back the heap and the symbol table,
unless the module asked to stay. The 128K VM (`PLVM.128`) puts
bytecode in auxiliary memory; everything here is the 64K VM.

## 3. Requirements, in order of importance

- R1. Load *existing* $6502 files: every current module keeps
  working, and ideally every one gets a streaming load.
- R2. Don't reduce the space programs have, or not by much: bytes
  resident while a program runs.
- R3. A modest, reviewable change; the maintainer has flagged the
  effort.
- R4. ccish mustn't presume which loader change lands: its own
  artifacts (measure.txt's "streamed" column, the launch scripts)
  stay loader-agnostic.

XLOAD meets R2, R3 (nothing in PLASMA changes) and R4; it meets R1
only for programs ccrel writes in its image format, which is all ccish
needs. R1 for everyone else is what option 1, proposed upstream, would
add later.

## 4. Facts the options rest on (checked in this tree)

1. **Definitions precede fixups in every writer.** xplasm's emit_rld
   writes all $02 entries first ("First emit the bytecode definition
   entrypoint information", src/toolsrc/codegen.c:398), then the
   fixups (:417, "Now emit the fixup table"), then the $00 terminator
   (:434), and emit_esd after that (:436-456). ccrel does the same
   (OBJECT.md:541-543; the $02 entries are src/link.c:1273 and :1584).
2. **The current loader is already two-phase for this reason.**
   loadmod loops over the $02 entries (cmd.pla:1111), then the fixups
   (:1121-1128), then the ESD (:1131-1156). A streaming loader that
   applies entries in file order makes the same patches in the same
   order.
3. **The heap is stack-like.** allocheap bumps a pointer and returns 0
   when the heap would reach the frame stack (cmd.pla:904-926);
   releaseheap pops back to a level (:943-946). Anything allocated
   above the body is released by the same `releaseheap(modend)`
   (:1158).
4. **External fixups are few.** In the four built modules (2026-11-11;
   counting RLD entries by type byte to the $00, then the ESD to its
   $00): cccomp has 51 of 3,036 entries, ccgen 50 of 2,246, ccpp 47 of
   1,923, ccrel 35 of 1,115; the ESDs are 120 to 129 bytes. A deferred
   list of 4 bytes per external fixup is 204 bytes or less for
   anything ccish builds.
5. **loadmod has two latent bugs.** It never checks allocheap's 0
   return before the memcpy (cmd.pla:1079-1080), and it never checks
   that the RLD and ESD it reads after the body (:1085-1088) fit below
   the frame stack: a file whose body fits but whose whole file
   doesn't overwrites the stack instead of failing. (Also in
   doc/plasma-requests.md, item 1.)
6. **The external patch is an addition.** The word at the spot holds
   an addend (OBJECT.md:296-298), and the loader writes `*addr = *addr
   + lookupextern(esd, rld->3)` (cmd.pla:1124; src/vmsrc/cmdsys.c:521).
   Internal fixups get the word plus `modofst`, or the def entry's
   address when the target is in the bytecode (cmd.pla:1124; lookupdef
   :716; reloc :769).
7. **CMD's layout** is section 2's map and boot. Def entries are
   5-byte `JSR $03D6` records (adddef, cmd.pla:995-1004); $03D6 is the
   VM's indirect entry, with language-card bank 2 (plvm02.s:299).
   ProDOS's MLI is at $BF00; CMD's own file I/O is raw MLI through its
   buffer at $0800 (cmd.pla:4, :868-902).
8. **CMD has a raw-binary path.** `-NAME` (execsys, cmd.pla:1304-1327)
   reads a file into $2000 and `exec()`s it (`BIT ROMEN; JMP $2000`,
   :217-220): a one-way door. A file beginning `JMP $EEEE` gets its
   name copied to $2006 (:1317-1322). $2000-$3FFF is hi-res page 1,
   and part of the heap.
9. **`+NAME` doesn't run a non-$6502 file.** loadmod dispatches on the
   magic (:1036); without it, init stays 0 and the file is loaded and
   kept (:1162-1166). Since loadmod first checks the ProDOS type is
   $FE (:1025-1029), a file of another type is refused ("incompatible
   type", $4A) before any of that.
10. **The cost of a CMD change is measurable.** xplasm is built
    (src/xplasm); CMD builds with `./xplasm -AOW vmsrc/apple/cmd.pla`
    and `acme --setpc 8192` (src/makefile:101, :241-243).
11. **CMD's frames stay on the stack while a program runs.** ccish's
    programs run `main` inside init, so the whole run happens inside
    loadmod's call to init, with loadmod's locals (about 233 bytes:
    the 128-byte `header`, the 64-byte `filename`, and 20 words),
    execmod's (about 73: a 63-byte name), and docmds' (3) on the
    frame stack: about 310 bytes, plus any per-frame overhead
    (plvm02.s's ENTER, still to read). `MEM FREE` was measured without
    them, and measure.py counts frames only from the program's init,
    so measure.txt's running margins are about 310 bytes too high.
12. **loadmod reads to EOF.** Its read loop (cmd.pla:1083-1088) runs
    until a read returns nothing, so anything appended to a module's
    file is read into memory too. A second-stage loader can't carry
    its payload in its own file.
13. **FILEIO's I/O buffers are page-aligned.** Each file beyond the
    first opens into a 1K buffer that `iobufalloc` takes from the heap
    with `heapallocalign(1024 * n, 8, ...)` (fileio.pla:208-237),
    which skips 1 to 256 bytes; ccish's fopen asks for one at a time
    (lib/fopen.c). measure.py counts each as 1,280 bytes, the worst.
14. **What CMDSYS exports** (cmd.pla:88-145 and the t_cmdsys table,
    :60-72; inc/cmdsys.plh): the heap functions (`heapmark`,
    `heapalloc`, `heapallocalign`, `heaprelease`, `heapavail`),
    `memcpy`, `memset`, `strcpy`, `strcat`, `call` (run native code),
    `syscall`, the console functions, and, through the table,
    `modexec`, `sysopen`, `sysread`, `sysclose`, `sysexec` and
    `lookupsym`. Not exported: `reloc`, `lookupdef`, `adddef`,
    `lookupidx`, `addsym` and loadmod itself.
15. **Bytecode branches are relative.** BRNCH and the conditional
    branches add a 16-bit offset to the IP (plvm02.s:1298-1313); JUMP,
    added for PLFORTH, is absolute.

## 5. The chosen option: XLOAD

Decided in discussion, 2026-11-11. The user's idea: a second-stage
loader. PLASMA's loader loads XLOAD, a small ordinary module; XLOAD
gets itself out of the way, loads the real program in place, streams
its relocation data, does the final fixups, and jumps to the program.
PLASMA's loader needn't change, since it only ever loads XLOAD.

### 5.1 What the user sees

```
+XLOAD CCCOMP FOO
```

where today it's `+CCCOMP FOO`. XLOAD is a prefix, the loader for
all four programs: one copy on the disk, one thing to test. ("RUN" is
BASIC's, and "GO" is common elsewhere; the name was chosen from
PRELOAD, GO, LAUNCH, XLOAD, STREAM and FETCH.)

- **Arguments.** lib/start.c builds `argv` by splitting CMDSYS's
  command line in place, so XLOAD takes its own word off the front of
  that buffer before the hand-off: the program sees `CCCOMP FOO`, and
  its `argv[0]` is `CCCOMP`, as now.
- **Files.** The payload is a file of its own (fact 12). It can keep
  the plain name `CCCOMP`, with a ProDOS type other than $FE, so that
  `+CCCOMP` without the prefix gets CMD's "incompatible type" error
  (fact 9) rather than a load that overwrites memory; XLOAD checks
  the type too. Which type, still to choose.
- **One program, or all four.** cccomp first, since it's the one that
  can't load; ccpp, ccgen and ccrel load today and can stay ordinary
  modules until XLOAD works on MAME. All four as images would save
  most of their RLDs on the disk (section 5.3).

### 5.2 How it works

XLOAD's file is an ordinary $6502 module in two parts:

1. **The first part**, a few hundred bytes, is all loadmod runs. It's
   a small relocator.
2. **The second part**, the real loader, is carried inside XLOAD's
   data as an image in the payloads' own format (5.3). loadmod sees
   only bytes and doesn't touch them.

The steps:

1. loadmod loads XLOAD (and FILEIO, its dependency) and calls its
   init.
2. The first part reads the payload's header for its size, then
   copies the second part's image to its new home, adds that address
   at each of its fixups, and looks up its imports with `lookupsym`.
   Since it's properly relocated, the second part is ordinary ccish C:
   globals, strings, helpers and calls all work. The first part then
   calls it, and isn't needed again.
3. The second part reads the payload's image straight into its final
   place, the bottom of the heap where XLOAD's own def table starts,
   over the first part and the original XLOAD module. It reads in
   chunks through a small buffer, then applies the sorted fixups and
   resolves the imports.
4. The hand-off (5.4) jumps to the payload's init. Nothing of XLOAD
   stays.

**Where the second part goes:** just above where the payload will
end, which the first part knows from the payload's header. Its frames
then grow from the current frame pointer as usual, with no change to
`IFP`. The payload's heap tables are later allocated over the dead
second part. The other place considered, just below the frame stack
(the user's first form), needs `IFP` lowered below it first, another
piece of native code; it's the fallback if memory is tighter than
expected.

**Why load in place:** the alternative (leave XLOAD at the bottom,
read and fix up the payload higher up for its final address, then
block-move it down) would let XLOAD stay unmoved, but adds the move:
about 16 cycles a byte, so about half a second for cccomp's 31.5K at
1 MHz, against several seconds of disk reads. "Apples aren't fast";
loading in place avoids it, and keeps the hand-off to restoring state
and a jump.

**Reusing CMD.** XLOAD uses only what CMDSYS exports (fact 14): the
heap functions, `memcpy`, `memset`, `call`, `lookupsym`, and file
reads through FILEIO. It needs none of CMD's internals (`reloc`,
`lookupdef`, `adddef`, `addsym`), since the payload's format avoids
what they do.

### 5.3 The payload's format, ours to choose

Both ends are ccish's, so the payload needn't be PLASMA's RLD
format. ccrel writes:

- **The image:** the program's def table, then its body, as one block
  correct for address 0. ccrel lays out the def entries itself (5
  bytes each, `JSR $03D6` and the address, in order), so references to
  functions are references to entries in the image.
- **The internal fixups,** then all one kind: add the load address.
  Sorted, and written as the gaps between offsets, mostly one byte
  each: about 3K for cccomp, where its RLD is 12.1K.
- **The imports:** each name (15 for cccomp) with its spots (51 in
  all), resolved with `lookupsym`.
- Possibly item 12's zero runs in the data (doc/plasma-requests.md).

### 5.4 The hand-off

With under 1K to spare while running, XLOAD can't stay resident, and
the payload can't return into XLOAD's code (its heap tables are
allocated over it). So the last step is a tail call: the payload's
init returns straight to loadmod, as if loadmod had called it.

That takes a small native routine, about 20 to 30 bytes of 6502 code,
kept as bytes in a data array and run through CMDSYS's `call`:

1. Restore the 6502 stack pointer, the evaluation stack index (X) and
   `IFP` to their values when XLOAD's init was entered.
2. `JMP` to the payload's init def entry, `JSR $03D6`, so the VM's
   indirect entry takes over.

When the payload's init returns, it lands in loadmod, which frees
XLOAD's (stale) init address; execmod then puts the heap back as
usual.

This rests on the VM's calling convention: the zero-page layout and
what the 6502 stack holds when init is called. **Reading plvm02.s's
CALL, ICAL, ENTER, LEAVE and RET handlers is the next step**, before
writing the routine. The first version's appendix inferred the
indirect entry's behavior (IINTRP pops the def entry's address and
loads the IP from it, plvm02.s:161-174; a function returns with a
bare RTS, :1458) but didn't read the CALL handler.

### 5.5 Memory and disk

At the load peak, for cccomp: the payload's image (def table 1,461 and
body 31,506: 32,967), the second part (about 2K, a guess), FILEIO
(1,998), a 512-byte buffer, and frames, CMD's included (about 400):
**about 37.9K**, leaving about 3.3K of the 41,171. Running, after the
hand-off, needs exactly what it does today.

On the disk, cccomp's payload would be about 33K of image plus about
3K of fixups, against its 43.8K module; XLOAD itself perhaps 3K, once.

### 5.6 Testing

plvm's VM is C, so the native hand-off can't run there as written.
Under plvm, XLOAD would end with a plain call instead, which still
tests the format, the relocation, the imports and the in-place load
with ccish's suite on the host. Only the hand-off routine needs MAME
(tools/a2screen, with its guards).

## 6. The other options

### Option 1: stream the current format, inside CMD's loader

After the body is allocated, don't read the rest of the file into
memory: read the RLD in chunks (a 1K buffer, or 4 bytes at a time)
and apply each entry as it arrives. $02 entries make def entries;
$8x fixups apply at once (the def table is complete, fact 1); $9x
fixups go on a deferred list (fact 4); at the $00, the stream is at
the ESD, whose imports resolve the deferred spots and whose exports go
in the symbol table. The same in plvm's load_mod (cmdsys.c:336, whole-
file read at :433).

- Pro: no format or writer change; every existing module streams (R1).
- Pro: the same patches in the same order as today (fact 2).
- Pro: small resident cost, perhaps 150-300 bytes of CMD (an estimate,
  to measure: fact 10); fixes both bugs of fact 5.
- Con: it changes CMD, a system file: the maintainer's review, merge,
  and a new system disk. 150-300 bytes resident for everyone.
- Con: load time: 12 reads of 1K instead of 3 of 4K for cccomp's RLD,
  a fraction of a second; 4 bytes at a time, a few seconds.
- **Now:** the right fix for PLASMA in general, worth offering
  upstream, but not needed by ccish once XLOAD works. Option 9 is a
  way to run it before it's merged.

### Option 2: a new format with the ESD first, inside CMD's loader

ESD before RLD, under a new magic word: the simplest streaming loop,
no deferred list; old files load as today.

- Con: the loader keeps both paths, more resident code than option 1
  (R2); every writer changes (xplasm, ccrel, the acme step); a new
  file on an old loader misbehaves (fact 9); old files still load the
  old way (R1 only for new files).
- **Now:** only worth it bundled with other format work (item 12).

### Option 3: a load shim, a separate loader module

The first version's name for what became XLOAD.

- **3a, a VM module that stays resident while the program runs:** its
  body and def table, perhaps 1.5-2.5K, stay for the whole run, more
  than the margin. XLOAD avoids this with the tail call (5.4).
- **3b, a native shim** run by `-NAME` into $2000 (fact 8), moved into
  a "low-RAM hole" below CMD. **There is no such hole** (section 8,
  corrections 1-3): $0200-$02FF, $0400-$07FF and $0800-$0BFF are all
  in use. Rejected.
- Its good points carry over to XLOAD: no change to the maintainer's
  code; a place to try loader ideas (the fits check of fact 5, item
  13, zero runs) before proposing any upstream; testable under plvm.

### Option 4: ccish fixes itself, table in the body

ccrel puts the fixups in the data segment; the module's init patches
its own words. The RLD keeps only the 291 $02 defs (1,165 bytes).

- At least 2 bytes per fixup: 2,745 fixups, 5,490 bytes, in the body.
  Budget: 41,171 - 31,500 (body) - 1,461 (def table) - 1,998
  (FILEIO) - 1,165 (def-only RLD) - 129 (ESD) = 4,918: **572 bytes
  short**, and the table stays resident. Rejected.

### Option 5: ccish fixes itself, sidecar fixup file

As option 4, with the table in a NAME.fx file that init streams.

- The first version rejected it for disk space (17-19K more: "152K
  of 136K usable"). XLOAD's format (5.3) is in effect this idea done
  properly: the fixups in a file of their own, compact, applied by a
  loader that then leaves.

### Option 6: split cccomp into two modules

Part 1 runs and loads part 2. The load peak is under 41,171 only if
part 2 holds under about 6K of the 31.5K body, for a margin that goes
to zero, and the halves must share all of the front end's tables.
Rejected.

### Option 7: shrink cccomp's file instead of streaming

Item 12 (zero runs): -4.3K, 42,933, still over. Plus item 14 (call
bytecode directly): about 41.8K, still over, and a VM change. A
relative call op would fit but is the largest change of all (7 VMs, 3
JITs). Rejected as the way to load; still worth doing for disk and for
the running margin.

### Option 8: pre-relocate the module (missed by the first version)

On a given disk, a module started from a fresh prompt loads at the
same place every time. So ccrel could apply the internal fixups itself
for that address, keeping in the RLD only the $02 entries and the
external fixups. cccomp's file would drop to about 33K and load with
today's loader with about 4.7K to spare, and the four programs would
save about 29.5K of disk.

- **Rejected by the user:** it breaks whenever anything before it
  changes size: CMD (2.20's ends 745 bytes after 2.21's), FILEIO, or
  a module someone ran and kept. A check at init (one kept fixup on a
  word holding the expected address) would make that fail cleanly,
  but it would still fail.

### Option 9: a patched CMD on ccish's own disk (missed by the first version)

The first version treated any loader change as needing the
maintainer's merge. But ccish's Apple II disk is ccish's to build, and
it can carry a CMD built from a cmd.pla with option 1's change: proof
on a real machine before asking upstream, and the fact 5 fix at once.
The cost: a CMD for each PLASMA release supported.

- **Now:** a fallback if XLOAD's hand-off proves unworkable.

## 7. What earlier work settled (from the first version's appendix)

1. **A function can be moved and still return normally.** The VM's
   indirect entry reads the function's address only at entry (IINTRP
   pops a pointer V and sets the IP to the word at V+1,
   plvm02.s:161-174), and a function returns with a bare RTS
   (:1458). So copying a function, rewriting its def entry and
   calling it works, the same pattern as CMD's own boot (cmdstub.s,
   CMDMOVE). Inferred from IINTRP and RET, not from the CALL handler:
   5.4's reading will confirm.
2. **The heap is the whole budget.** CMD's image ends at $1B45 in this
   tree (3,909 bytes from $0C00), and the heap runs from there to the
   frame stack: the 41,171 free is all there is. The 2.20 disk's
   `MEM FREE` implies a heap start of $1E2D, 745 bytes later than this
   tree's, so this tree's CMD would give about 41.9K free (not
   measured).
3. **The system's one memory hole costs more than it gives.** The
   `reshgr1` flag (cmd.pla:908-913) moves allocations that straddle
   $2000-$3FFF up to $4000: it frees hi-res page 1, but takes 8K from
   the heap, leaving 33,792 bytes for a program that needs 40,191.
4. **Toolchain.** xplasm (src/xplasm) builds CMD; /usr/bin/acme 0.97
   assembles cmdstub.s and cmd.a but not plvm02.s ("Target out of
   range" at :1355-1358), so the VM can't be rebuilt here. The
   makefile copies vmsrc/c64/plvmzp.inc to vmsrc/plvmzp.inc before
   assembling CMD. bin/ac-linux-aarch64-15.2 is AppleCommander (a disk
   image tool). Untracked files from that measurement remain:
   src/vmsrc/apple/cmd.a, cmd.sym and cmd_test.bin, and
   src/vmsrc/plvmzp.inc. A later check (2026-11-11) assembled FILEIO
   in a scratch directory: 1,998 bytes resident by loadmod's own
   accounting (body without header, def table with init's entry and
   end byte).

## 8. Corrections to the first version

1. **The 40-column text screen is at $0400-$07FF, not $2000** (its
   fact 8). $2000-$3FFF is hi-res page 1. So its "largest low-RAM
   hole", $0400-$07FF, is the visible screen, which ccish writes its
   messages to.
2. **$0900-$0BFF isn't a hole:** it's the last 768 bytes of the 1K
   ProDOS buffer at $0800, in use during every read, CMD's included.
3. **$0200-$02FF isn't free:** it's the input line buffer, and execsys
   uses $0280. So there are no low-RAM holes, and option 3b had no
   home.
4. **cmdstub.s copies 17 pages ($0C00-$1CFF), not 16.** Harmless,
   since the heap starts at `lastdef`, inside that copy.
5. **Its running margins were about 310 bytes too high** (fact 11):
   cccomp's is about 670, not 980.
6. **It missed options 8 and 9**, and the two-stage loader the user
   proposed (section 5).
7. **Its view that every loader option needs the maintainer** missed
   that ccish's disk is its own (option 9), and that a loader ccish
   ships can use a format of ccish's own (5.3).

## 9. Still to learn

1. **The VM's state at init entry** (plvm02.s: CALL, ICAL, ENTER,
   LEAVE, RET, IINTRP): the 6502 stack's contents, X, and `IFP`, for
   the hand-off (5.4); and ENTER's per-frame overhead, for fact 11.
   **Next.**
2. The ProDOS file type for payloads.
3. Whether FILEIO's init allocates anything on the heap (it would sit
   between CMD and XLOAD).
4. How small the first part can be, and the second part's real size.
5. Which disk is the target: the 41,171 is from the 2.20 disk; this
   tree is 2.21, whose CMD is 745 bytes smaller.
6. Whether cmdsys itself is in the $DA00 symbol table that
   `lookupsym` searches (the first version's open item 6), since the
   payload imports CMDSYS's functions by name.

## 10. Plan

1. Read plvm02.s (9.1), and write down the hand-off routine.
2. Give ccrel a way to write a payload: the image, the sorted fixups,
   the imports.
3. Write XLOAD in ccish C: the first part, and the second part as an
   image inside it.
4. Test under plvm with ccish's suite (a plain call in place of the
   hand-off), then on MAME with tools/a2screen, booting a disk with
   XLOAD and cccomp's payload.
5. Then the other three programs, if the disk savings are worth it.
6. Offer option 1 upstream separately, for everyone else's large
   modules, with fact 5's fixes.
