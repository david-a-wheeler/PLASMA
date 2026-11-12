# Streaming the RLD: options for discussion

cccomp, ccish's front end, cannot load on a 64K Apple II. Its module
file is 43,799 bytes, and every loader reads the whole file into the
heap before anything runs. Free memory at the PLASMA 2.20 prompt
measures 41,171 bytes (doc/plasma-requests.md:400, measured 2026-11-09
on MAME's enhanced IIe; approach.md, "Free memory, measured"). The
load needs 47,233 (doc/measure.txt:137), which is 6,062 short. With a
streaming load the need is 34,959, which leaves 6,212 to spare
(measure.txt:137). Running needs 40,191, which leaves 980 to spare, a
lower bound (measure.txt:135).

This document puts the ways to get the streaming load on the table,
with costs, for discussion. It complements the issue draft
(,streaming.md at the repository root) and
https://github.com/dschmenk/PLASMA/issues/78, where the maintainer
replied "So this would be a pretty significant update" and asked
whether a 128K machine would be the baseline (issue #78, dschmenk,
2026-11-11). The answer on the record: ccish already runs and
bootstraps under the portable VM, and the goal is a 64K machine
(issue #78, david-a-wheeler, 2026-11-11, with the fit table).

## Requirements, in order of importance

- R1. Load *existing* $6502 files (the issue's "option 1"): every
  current module keeps working, and ideally every current module
  gets the streaming load.
- R2. Not reduce the space available to programs, or not by much:
  bytes resident while a program runs.
- R3. A modest, reviewable change; the maintainer has flagged the
  effort.
- R4. ccish must not presume which specific loader change lands:
  ccish's own artifacts (measure.txt's "streamed" column, the launch
  scripts) stay loader-agnostic.

## Facts the options rest on (verified in this tree)

1. **Definitions precede fixups in every writer.** xplasm's emit_rld
   writes all $02 entries first ("First emit the bytecode
   definition entrypoint information", src/toolsrc/codegen.c:398),
   then the fixups (:417, "Now emit the fixup table"), then the $00
   terminator (:434), and emit_esd after that (:436-456). ccrel does
   the same: "Write the RLD (function entries in code order, the
   helpers' included, then the kept relocations, those in the data
   first) and the ESD" (OBJECT.md:541-543; the $02 entries are
   src/link.c:1273 and :1584).
2. **The current loader is already two-phase for this reason.**
   loadmod runs a loop over the $02 entries (src/vmsrc/apple/cmd.pla:
   1111), then a loop over the fixups (:1121-1128), then the ESD
   (:1131-1156). A streaming loader that applies entries in file
   order is therefore semantically identical to the current
   in-memory loader: same entries, same order, same patches. That
   equivalence is the review story.
3. **The heap is stack-like.** allocheap bumps a pointer and returns
   0 on overflow (cmd.pla:904-926); releaseheap pops back to a level
   (:943-945). A side table allocated above the module body is
   released by the same `releaseheap(modend)` (:1158) that today
   releases the RLD region: transient, never resident.
4. **External fixups are few.** Scanning the four built modules
   (ccish/tools/out/{ccpp,cccomp,ccgen,ccrel}, 2026-11-11; parse the
   header, count 4-byte RLD entries by type byte to the $00, then
   the ESD to its $00): cccomp has 51 of 3,036 entries, ccgen 50 of
   2,246, ccpp 47 of 1,923, ccrel 35 of 1,115; the ESDs are 120 to
   129 bytes. A deferred list of 4 bytes per external fixup (the
   spot, 2 bytes; the ESD index, 1; the entry type, 1) is 204 bytes
   or less for anything ccish builds. It grows with a module's
   imported call sites, not its size.
5. **loadmod has two latent bugs.** It never checks allocheap's 0
   return before the memcpy (cmd.pla:1079-1080), and it never checks
   that the RLD+ESD it reads 4,096 bytes at a time into memory after
   the body (:1085-1088) fits below the frame stack: a file whose
   body fits but whose whole file does not overwrites the stack
   instead of failing. Both are small fixes to include with any
   change here.
6. **The external patch is an addition.** The word at the spot holds
   an addend (0 for a plain reference; OBJECT.md:296-298: "For a
   global, the word holds the addend"), and the loader writes
   `*addr = *addr + lookupextern(esd, rld->3)` (cmd.pla:1124;
   src/vmsrc/cmdsys.c:521). Internal fixups get the word plus
   modofst, or the def-table entry's address when the target is in
   the bytecode region (cmd.pla:1124, lookupdef at :707-751; the
   per-entry core is the reloc asm at :753-833). A deferred entry
   must therefore carry enough to replay the same patch after the
   ESD has been read.
7. **CMD's layout, for the options that depend on it.** The VM's
   boot reads the CMD file into $2000 (src/vmsrc/apple/plvm02.s:
   237-253, "READ CMD INTO MEMORY"; READPARMS at :261-265 reads into
   $2000); CMD then moves itself to $0C00-$1BFF (src/vmsrc/apple/
   cmdstub.s:16-18, "MOVE CMD DOWN TO $0C00-$1C00", final jump at
   :45). `heap` is `@lastdef`, right after CMD's image (cmd.pla:
   1426), which is why "every byte added to the loader comes out of
   free memory" (,streaming.md, "Either way"). The symbol table is
   $DA00 (cmd.pla:5) with `lastsym` in CMD's data (:147). Def-table
   entries are 5-byte `JSR $03D6` records (cmd.pla:995-1004); $03D6
   is the VM's "INDIRECT INTERP ENTRY" with language-card bank 2
   (plvm02.s:299). ProDOS MLI is $BF00 (plvm02.s:17); CMD's own file
   I/O is raw MLI through its buffer at $0800 (cmd.pla:4, :868-902).
8. **CMD has a raw-binary path.** The `-NAME` command (execsys,
   cmd.pla:1304-1327; dispatched at :1392) reads a file unmodified
   into $2000 (:1313) and then `exec()`s it, which is `BIT ROMEN;
   JMP $2000` (:217-220): a one-way door. A file beginning `JMP
   $EEEE` gets its name copied to $2006 (:1317-1322). $2000-$3FFF
   is below the heap, so a program there costs no heap; it does
   overwrite the 40-column text screen, which starts at $2000 in
   the standard Apple II map.
9. **`+NAME` does not execute a non-$6502 file.** loadmod dispatches
   on the magic at :1036; the non-extended path leaves init at 0 and
   returns modkeep (:1162-1166; execmod at :1329-1350), so the file
   is loaded and kept, nothing is called. A file with an unknown
   magic handed to `+NAME` is thus silently loaded and kept, and
   with the bug in fact 5 that is a heap overwrite on a 64K machine,
   not an error. Relevant to any new-magic option below.
10. **The cost is measurable before it is proposed.** xplasm is
    built (src/xplasm, an aarch64 ELF binary); CMD builds with
    `./xplasm -AOW vmsrc/apple/cmd.pla` and `acme --setpc 8192`
    (src/makefile:101, :241-243). A scratch build of a modified
    cmd.pla gives the code cost as a number, not an estimate.

## Option 1: stream the current format, inside the loader

The change in loadmod (cmd.pla, and the same shape in plvm's load_mod
at src/vmsrc/cmdsys.c:336, whose whole-file read is at :433): after
the body is allocated and the first 128 bytes copied, do not read
the rest of the file into memory. Read the RLD in chunks (a fixed
1K scratch buffer; 4 bytes at a time is the minimal variant) and
apply each entry as it arrives:

- $02: record the def (today's first loop, cmd.pla:1111-1118).
- $8x: apply the internal fixup now (the def table is complete by
  construction, fact 1).
- $9x: push (spot, ESD index, type) on the deferred list (fact 4).
- $00: the RLD terminator (a single byte, after the last 4-byte
  entry; codegen.c:434). The stream is now at the ESD. Scan it entry
  by entry: an import whose index is deferred resolves and patches
  those spots (lookupextern, cmd.pla:980-994; the ESD entry is a DCI
  name, a flags byte, a word: lookupidx at :662-699); an export is
  added to the symbol table (today's last loop, :1131-1156).

Then releaseheap(modend) as today, and enter init as today
(:1162-1176).

- Pro: no format change, no writer change; every existing module
  streams (R1), including the whole PLASMA system.
- Pro: the semantics are the current loader's (fact 2): the diff is
  "same loop, input from the file instead of a buffer, one small
  side list for the one case the file order inverts".
- Pro: the smallest permanent cost of any option. The 4K read loop
  (:1085-1088) becomes a 1K-buffer loop, the three in-memory loops
  become the streaming loop; net perhaps 150-300 bytes of CMD code
  (the issue draft's 150-250 estimate, ,streaming.md), to be
  replaced by a measured number in the prototype (fact 10).
- Pro: the deferred list is transient (fact 3) and under 1K for
  realistic modules; its worst case (every fixup external) still
  costs less than the 12K of RLD it replaces.
- Pro: fixes both latent bugs of fact 5 for free.
- Con: it changes CMD, a system file: maintainer review, merge,
  re-cut system disk. The other loaders (cmdjit.pla, sossys.pla,
  a1cmd.pla, c64/cmd.pla) can follow later; cmd.pla plus plvm's
  cmdsys.c are enough for ccish, and plvm's copy is what makes the
  logic testable under ccish's whole suite and the bootstrap test
  before any 6502 byte is written.
- Con: 150-300 bytes resident in CMD forever (R2), taken from
  everyone's 41,171: 0.4-0.7%.
- Con: load time. cccomp's RLD+ESD region is 12.3K: 3,036 MLI reads
  of 4 bytes, or about 12 of 1K, instead of 3 of 4K today. The 1K
  form adds a fraction of a second (the region is about 12 ProDOS
  blocks, read in order); the 4-byte form, a few seconds.

## Option 2: a new format with the ESD first, inside the loader

ESD before RLD, under a new magic word (the loaders dispatch on the
magic today, cmd.pla:1036; the value is arbitrary, $6503 was only a
placeholder). The streaming loop is the simplest possible: read the
small ESD, then apply every RLD entry as it arrives, no deferred
list. Old files load exactly as today.

- Pro: the simplest streaming code of any option.
- Con: the loader must keep the old path forever in addition to the
  new one: more resident code than option 1, not less (R2).
- Con: every writer changes (xplasm's codegen, ccrel, the acme step),
  and a new-format file handed to an old loader is the silent heap
  overwrite of fact 9: the format change has to ship with the loader
  everywhere the file might be loaded.
- Con: existing files still load the old way: R1 is met only for new
  files.
- Net: option 2 is option 1's streaming loop plus a format migration,
  for a saving of the deferred list (about 200 bytes transient,
  about 50 bytes of code). It earns its keep only bundled with other
  format work (doc/plasma-requests.md, item 12, zero runs in the
  data segment) as a separate future step, not as the streaming
  vehicle.

## Option 3: a load shim, a separate loader module

A standalone loader shipped by ccish, run by CMD by the normal
mechanism. It opens the target file itself, streams it (option 1's
loop, or option 2's simpler loop), and enters the target's init
through a def entry exactly as loadmod does (cmd.pla:1162-1166:
`adddef(init, @deflast)()`); when the target returns, the shim
returns the status to CMD. Two delivery forms:

- **3a: a VM module shim** (`+ccload cccomp`). Written in PLA:
  reviewable, and portable, since the same source runs under plvm.
  That means ccish's full test suite and the bootstrap test
  exercise the streaming logic on the host. It declares FILEIO as a
  dependency, so CMD loads it first (the dependency load at
  cmd.pla:1044-1056); its own file is a few KB and loads fine by the
  old mechanism.
- **3b: a native 6502 shim** (`-ccload cccomp`), a raw binary read
  by execsys into $2000 (fact 8). It costs no heap: cccomp keeps its
  full 980-byte margin. It is self-contained: MLI through $BF00, the
  heap pointer at @lastdef, the symbol table at $DA00 with lastsym,
  the VM's indirect entry $03D6 with bank 2 (fact 7): all fixed
  addresses of the shipped system. **As written it does not work**:
  the shim runs from $2000 while cccomp's body allocation grows up
  through $2000-$3FFF (the heap is contiguous from the end of CMD's
  image to the frame top, and the body crosses $2000 about 1.6K into
  the read), overwriting the running shim. The fix is to relocate the
  shim into the low-RAM holes below CMD as its first act (the largest
  is $0400-$07FF, 1K; appendix), which requires the shim to fit in
  about 1K.

A format sub-choice for either form: **F1**, stream the current
$6502 format (the deferred list; loads existing files; no writer
change; no footgun) versus **F2**, a new-magic ESD-first format
(the simplest loop; ccrel writes it; `+ccomp` on such a file is the
silent overwrite of fact 9).

- Pro: zero changes to the maintainer's code (R3 at maximum): the
  maintainer need not review or merge anything; the shim is a file
  on ccish's disk. This sidesteps the "pretty significant update"
  objection entirely.
- Pro: a sandbox for loader R&D: the missing fits check (fact 5),
  item 13 of doc/plasma-requests.md (run main after init and free
  the startup code), and the zero-run format can be prototyped in
  the shim before any is proposed upstream. The shim's code is the
  future PR's draft.
- Pro (3a): testable under plvm with ccish's suite before it runs on
  a real machine.
- Pro (3b): the full running margin; no shrinkage of cccomp needed.
- Con (3a): the shim's init runs the target and cannot return until
  it is done, so the shim's body and def table stay resident for the
  whole run. The body is comparable to loadmod (the largest function
  in cmd.pla, :1005-1180), plus argument parsing and the error
  paths, and the def table is 5 bytes per function of the shim, kept
  (cmd.pla:1061, defcnt*5+1): 1.5-2.5K estimated, to be measured.
  That spends the 980-byte margin and then some. 3a fits only if
  cccomp shrinks another 1-2K (it has been shrinking: 28,321 to
  25,854 bytes of code in two days, doc/plasma-requests.md:83-85) or
  items 13/14 land.
- Con (3b): 6502 assembly to review (the PLA form is the readable
  one); not portable to plvm, so the 6502 port is verified only on
  MAME or a real II; the execsys path is a one-way door (fact 8), so
  a return-to-prompt mechanism is needed: the $EEEE marker
  convention or a jump to CMD's $0C00 re-entry (cmdstub.s:45), to be
  settled in the prototype; and it encodes CMD/VM internal layout as
  fixed addresses, so it is version-coupled to the PLASMA release it
  ships with. The low-RAM home is unverified (appendix): the holes
  are undocumented, and a ProDOS second-drive buffer may occupy
  $0400-$07FF on a multi-drive machine.
- Con (both): a second artifact on the ccish disk, which is already
  133.4K of the ~136K usable (the four module files are 122.2K,
  ccish.msg is 11,457 bytes; 140K = 143,360 bytes, minus 4,096 for
  directory blocks). A 2-4K shim fits only by squeezing, with item
  12's ~12K saving (doc/plasma-requests.md:336-352), or on a second
  drive, which the stated 2-to-4-drive setup makes tolerable.
- Con (F2): the footgun of fact 9. Con (F1): the deferred list.
- Con (both): only ccish's modules get the streaming load; R1 for
  other programs is unmet, and cccomp's launch must always go
  through the shim (a driver script can make that the only way,
  which is what R4 asks for anyway).

## Option 4: ccish fixes itself, table in the body

No PLASMA change, no shim: ccrel emits the fixups as a table inside
the data segment; the module's init finds its own load address in
the symbol table (loadmod adds the module's name before init runs,
cmd.pla:1093, the definition at :950-961), walks the table and patches
its own words, and registers its exports. The RLD keeps only the $02 defs
(291 in cccomp, 1,165 bytes): the def table is the VM's call
dispatch (fact 7), and it cannot go away without a VM change
(doc/plasma-requests.md, item 14).

- Numbers: the table needs at least 2 bytes per fixup (the spot):
  cccomp has 2,745 fixups (fact 4: 3,036 entries minus 291 defs
  minus 51 externals, the external ones in the table too), so 5,490
  bytes minimum, in the body. Budget for the body: 41,171 free,
  minus 31,500 (body, measure.txt:132), minus 1,461 (def table,
  :132), minus 1,998 (FILEIO, :133), minus 1,165 (def-only RLD),
  minus 129 (ESD) = 4,918. **572 bytes short of fitting.**
  Delta-encoding the spots (they are monotonically ordered) might
  close it, at the cost of more init code and time.
- Pro: no PLASMA change, no extra disk, no resident shim.
- Con: does not fit today; it also costs 1-2 seconds of
  self-patching per load (2,745 patch iterations at init),
  duplicates the symbol-table mechanics in ccish, loses the
  loader's error checking, and helps no other PLASMA program.
- Verdict: revisitable if the RLD-reduction projects land (the
  base-plus-index references and the relative call op, ccish-rld.md,
  "Can it come down?").

## Option 5: ccish fixes itself, sidecar fixup file

As option 4, but the table lives in a NAME.fx file that init opens
and applies as it streams it. The .mod file is then body plus
def-only RLD plus ESD, about 32.8K, which loads today with about
4.9K spare, and the fixups cost no memory at all.

- Pro: no PLASMA change; the most memory margin of the
  self-contained options (load peak about 37.5K, the fixups read 4
  bytes at a time into scratch).
- Con: 17-19K on the ccish disk (one table per module: at least 2
  bytes per fixup of each, about 6.8K for cccomp and 12.5K for the
  other three): 152K of 136K usable. **It does not fit on one 140K
  disk**, and not on one even with item 12's 12K saving (140K).
  It needs a second drive.
- Con: two files per program; a missing .fx is a dead compiler (the
  ccish.msg precedent, diag.c's msg_text, handles a missing
  optional file, but this one is essential); 1-2 more seconds of
  disk time at init; the same self-patching and symbol-table
  duplication as option 4.
- Verdict: a fallback of last resort, and only with two drives.

## Option 6: split cccomp into two modules

ccomp part 1 runs, loads part 2 over it (modexec, the mechanism
tools/ccstat.pla uses today), part 2 finishes the job, both return
and are freed.

- Numbers: with a = part 1's body in K, the load peak when part 2
  loads is part 1 resident (its body a, its proportional share of
  the def table and heap globals, 1.46K and 2.39K, FILEIO at 2K,
  one I/O buffer at 1.3K; measure.txt:132-134) plus part 2's whole
  file (its body, its proportional share of the 12.1K RLD, the ESD)
  plus part 2's def table and heap: 50.9 - 0.38a K. That is under
  41.171 only for a at least about 25.3: part 2 must hold under
  about 6K of the 31.5K body (under 20%), a tail split, for a margin
  that goes to zero at the break-even point (a = 27 gives 0.7K). The
  running peak is unchanged by the split (both halves resident
  either way).
- Con: the two halves must share the type, name, and IR state across
  a module boundary: a restructuring of the front end's
  architecture. No AST exists to hand off; the state is the tables
  themselves (issue #78, david-a-wheeler, 2026-11-11: "It never
  generates an AST; ASTs are conventional but space killers").
- Verdict: tighter than streaming and much more work. Rejected.

## Option 7: shrink cccomp's file instead of streaming

- Item 12 (zero runs in the data segment, doc/plasma-requests.md:
  336-352; it is a loader and writer change anyway): -4.3K in
  cccomp's file. 47,233 - 4,300 = 42,933. Still over.
- Plus item 14 (call bytecode directly: drops the 291 $02 entries
  from the file and the def table from memory): the file comes to
  about 41.8K, still 0.6K over, and item 14 is a VM change in every
  VM and JIT.
- A relative call op (ccish-rld.md, "Can it come down?": "a VM
  opcode change in every VM (plvm.c, the 6502 ROMs, the loaders)")
  would remove the call fixups outright (about 6.9K, to about 36K:
  fits), but that is the largest change on this list: 7 VMs and 3
  JITs (doc/plasma-requests.md, item 3).
- Verdict: zero runs is worth doing on its own (12.4K of disk
  across the four modules), but shrinking alone cannot get cccomp
  under 41,171 without VM changes larger than the loader change.

## Summary

| Option | PLASMA change | cccomp margin (running) | Disk | New resident code |
|---|---|---|---|---|
| 1. Stream $6502 in the loader | cmd.pla, cmdsys.c | 6,212 | 0 | ~150-300 B in CMD (measured first) |
| 2. New format in the loader | cmd.pla, writers | 6,212 | 0 | more than 1 (two paths kept) |
| 3a. VM shim, F1 | none | ~0 to -1,500 | +2-4K (tight) | shim ~1.5-2.5K while target runs |
| 3b. 6502 shim, F1, relocate first | none | 980 (if the low-RAM home verifies) | +2-4K (tight) | 0 heap; shim must fit in ~1K low RAM |
| 4. Self-patch, in-body table | none | n/a: 572 B short | 0 | init ~0.3K |
| 5. Self-patch, sidecar file | none | ~3,600 at load | +17-19K: does not fit one disk | init ~0.3K |
| 6. Split cccomp | none | ~0 at best; part 2 under 6K | 0 | restructuring |
| 7. Shrink the file | item 12; VM for 14 | n/a: 1.7K short alone | -12K | loader and writer for 12 |

## Recommendation

Option 1 with F1's mechanics: stream the current format, deferred
external fixups, a 1K chunk buffer. It is the only option that meets
R1-R4 together; its permanent cost is the smallest and is measurable
before it is proposed (fact 10); and its review story is the
invariant "entries applied in the same order as today, input from
the file instead of a buffer" (fact 2).

Option 3 is the fallback if the maintainer declines option 1, and
the natural staging ground either way: build the streaming loop
once (it is the same loop), prove it under plvm with ccish's suite
and the bootstrap test, run the 6502 form on MAME (tools/a2screen),
and only then either propose the small CMD diff (option 1) or ship
the shim (3b for the margin, 3a once cccomp has shed another kilo).

Rejected with numbers: 4 (572 B short), 5 (16K short of disk), 6
(break-even at zero margin with a front-end restructure), 7
(1.7K short without larger VM changes).

## Appendix: what the 2026-11-11 discussion settled, and what is still open

### Learned

1. **The relocation mechanism works.** Answer to whether a shim can
   move itself out of the heap and continue as the module's init:
   yes. The VM's indirect entry reads the function address only at
   entry (IINTRP pops a pointer V and sets IP to the word at V+1,
   plvm02.s:161-174), and a function's return is a bare RTS
   (plvm02.s:1458). So an init can copy its body to a safe home,
   rewrite its def entries to point there, release its body from the
   heap (keeping its def table), push a pointer at the relocated
   address word, and JMP $03D6: the relocated code runs under the
   original 6502 frame, and its return lands in loadmod
   (cmd.pla:1166) as if it never moved. No initkeep is needed: a
   plain status return makes execmod (cmd.pla:1338-1348) restore the
   heap and symbol table to the pre-shim level, and loadmod's
   releaseheap(init) at the stale address (cmd.pla:1169) is a pointer
   move that gets overwritten. It is the same pattern as CMD's own
   boot (cmdstub.s:16-45, CMDMOVE).
2. **The 64K map leaves no hole for a resident loader.** Measured by
   assembling the current tree (xplasm, acme 0.97): CMD's image is
   $0C00-$1B44 (3,909 bytes), copied into the fixed 4K region
   $0C00-$1BFF (cmdstub.s:18-37, 16 pages; the comment "STOP WHEN
   DST=$2000 REACHED" is stale). The heap starts at `lastdef`, a
   dummy function at the end of the image (cmd.pla:1419-1426, "Dummy
   definition to get free heap"), so the heap is contiguous from
   about $1B44 to @fp $BF00: the measured 41,171 free is the whole
   program budget, with nothing left over. (MEM FREE:$A0D3 on the
   v2.20 disk implies heap start $1E2D; this tree's image ends 745
   bytes earlier, so the current tree has about 41.9K free:
   unverified.) No allocation sequence can leave $2000-$3FFF free:
   cccomp's def table alone (1,461 bytes) crosses $2000, and the
   body crosses it about 1.6K into the read.
3. **The one hole the system knows costs more than it gives.** The
   reshgr1 heap reshape (cmd.pla:908-913) moves allocations that
   straddle $2000-$4000 to $4000 and above: it would free
   $2000-$3FFF for a relocated shim but removes 8K of heap capacity,
   leaving 33,792 bytes for a program that needs 40,191: about 6.4K
   short. Dead end.
4. **The remaining candidates are the low-RAM holes below CMD**,
   about 2K in all, fragmented: $0200-$02FF (256 bytes), $0400-$07FF
   (1K), $0900-$0BFF (768 bytes). Known occupants around them: the
   zero page, the 6502 stack $0100-$01FF, STRBUF $0300 and the
   INTERP vectors $03D0-$03DF (plvm02.s:33, :42), the ProDOS
   iobuffer $0800 (cmd.pla:4, active during every MLI read, so a
   shim living there dies on its own first read), CSW $36/$37 and
   PROMPT $33. None of it is documented; it needs the item 15
   measurement on MAME.
5. **Toolchain.** xplasm (src/xplasm) builds CMD fine. /usr/bin/acme
   0.97 assembles cmdstub.s plus cmd.a but fails on plvm02.s (Target
   out of range at :1355-1358, the dispatch's backward branches), so
   building the VM with the available tools is blocked; the CMD
   measurement stands on its own. The makefile copies
   vmsrc/c64/plvmzp.inc to vmsrc/plvmzp.inc before assembling CMD.
   bin/ac-linux-aarch64-15.2 is AppleCommander (a disk image tool),
   not an assembler. Untracked build artifacts left by the
   measurement: src/vmsrc/apple/cmd.a, cmd.sym, cmd_test.bin, and
   src/vmsrc/plvmzp.inc.

### Still to learn

1. **The low-RAM map on the target** (item 15, MAME 64K IIe, with 1
   and 2 drives): is $0400-$07FF free, and does it stay free (ProDOS
   may place drive 2's buffer there)? This decides every
   relocate-first shim variant.
2. **How small the streaming shim can be**: loadmod's 6502 size as a
   benchmark (the assembled symbol table does not carry per-function
   names; needs an xplasm listing or a measurement pass). The
   low-RAM home fits about 1K.
3. **The return-to-prompt mechanism of the `-` path** (fact 8's
   one-way door): what $EEEE is (the marker at cmd.pla:1317 points
   into the VM bank; not found in source here), or whether a jump to
   CMD's $0C00 re-entry (cmdstub.s:45) cleanly restarts the prompt.
4. **The exact 6502 frame of a VM CALL** (how many words the CALL
   handler pushes, what the RTS return value is): inferred from
   IINTRP and RET, not read directly (the CALL handler is in the
   dispatch page, plvm02.s near :140). The relocation mechanism of
   appendix item 1 should be confirmed by a prototype on MAME.
5. **Which disk is the test target**: the 41,171 figure is from the
   v2.20 disk (mame/ setup); this tree is v2.21 with a 745-byte
   smaller CMD image. The margin figures shift by that much.
6. **The target's dependencies under a shim**: cccomp declares
   cmdsys and fileio. FILEIO loads as the shim's own dependency
   (cmd.pla:1044-1056), but whether the VM itself (cmdsys) is in the
   $DA00 symbol table that the dependency check queries is
   unverified.

### Net effect on the options

3b as first written is broken (it overwrites itself); the viable
shim forms are the relocate-first variants (3b-v2 native, or a
relocated 3a), all conditional on "still to learn" 1-2. Option 1 is
unaffected by any of this and remains the recommendation: it needs
no hole at all, because the streaming buffer is transient (fact 3)
and the deferred list is a few hundred bytes.

## If the decision is "go ahead"

1. Branch `c-compiler-streaming` off `c-compiler`; the loader work
   lives only there, so nothing outside ccish/ lands in master or
   c-compiler, and the loader commit stands alone for cherry-pick or
   PR.
2. Measure first: a scratch build of the modified cmd.pla (fact 10)
   replaces the 150-300 estimate with a number.
3. Implement in plvm's load_mod (C) first, the identical algorithm,
   and run ccish's full suite and `make bootstrap` under it: that is
   the host-side proof of the in-order-application invariant,
   thousands of loads.
4. Port to cmd.pla (PLA, reusing reloc's per-entry core, fact 6),
   build, and verify on the MAME IIe booting a PLASMA system disk:
   the stock system modules (regression: old files load and run)
   and the ccish passes (streaming).
5. Draft the PR for issue #78: the diff, the measurement, the
   invariant, and the two latent-bug fixes of fact 5.
