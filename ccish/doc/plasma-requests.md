# Requested changes to PLASMA, outside ccish

ccish works around most of these today. The first doesn't have a
workaround: it's what stops ccish from loading on a real 64K Apple II.
The rest would make ccish (and other PLASMA programs) simpler, faster,
or easier to test. They're in PLASMA's own tools: plvm (src/vmsrc),
the 6502 VMs, and xplasm. The most useful are first. (Started
2026-11-07; the details behind each are in approach.md, issues 21 and
22, and doc/bootstrap-test.md's "Verified VM facts".)

## What each would let ccish drop

ccish's workarounds all live in ccish (decided 2026-11-08: get ccish
working first, and make the case for these with it). Each request
below would remove one:

| Request | The ccish workaround it removes |
|---|---|
| 1. Stream the RLD | None possible: cccomp can't load on a 64K Apple II |
| 2. plvm exits with the status | tools/ccstat.pla, its marker, and tools/ccrun's parsing (and xplasm and acme in ccish's build) |
| 3. Unsigned comparison ops | Mainly speed, not size: a CMDSYS call (3 bytes, about 123 cycles) for every unsigned and pointer `<`, `>`, `<=`, `>=`, where an op would be 1 byte and about 44 cycles; counted loops limited to `int` |
| 4. Reject a bad import | Nothing to drop, but import mistakes found at load, not at run time |
| 5. Stop on an eval stack error | Nothing to drop, but a compiler bug can't pass a test |
| 6. A way to unwind | lib/exit.c's plvm path, its marker and BRK, and init's setting TMP: about 23 bytes in every program (on the 6502 VMs, lib/exit.c unwinds already) |
| 7. FILEIO fidelity | The library's ignoring of `create` and `destroy` results (lib/fopen.c) |
| 11. A stderr route | Error messages mixed into stdout under plvm |
| 12. Zero runs in the data segment | About 12.4K of zeros in ccish's four module files (4.3K in cccomp's) |
| 13. Run `main` after init | One-time startup code held in memory for the whole run |
| 14. Call bytecode directly | A 5-byte def entry for each function (1,465 bytes in cccomp, kept while it runs) |
| 15. A map of free memory | Guesses about what a 64K Apple II has free |

## Priorities

For getting cccomp to fit on a 64K Apple II, these matter most, in
this order:

1. **Item 1, streaming the RLD.** Without it cccomp can't load at all:
   its whole 44K file must fit in memory at once. With it, the load
   peak drops to the module body, about 32K.
2. **Item 14, calling bytecode directly.** It's the one that shrinks
   what cccomp needs while it runs, by about 1.4K (the def entries),
   where the margin after streaming is under 2K. It also makes every
   call faster.
3. **Item 12, zero runs in the data segment.** It cuts cccomp's file
   by about 4.3K, which lowers the load peak by as much (useful until
   streaming exists, and in any loader that reads whole files), and
   ccish's four files by about 12.4K on a 140K floppy. Once the RLD
   streams, it doesn't change the memory cccomp needs while running.

Item 15, measuring free memory, says how much of this is needed;
item 13 would add margin too. The rest (2 to 11) make ccish simpler,
faster or easier to test, but don't affect the fit. Item 3 is mainly
for speed.

## 1. The loaders: stream the RLD (needed to run ccish on an Apple II)

**What they do.** Every loader (`loadmod` in src/vmsrc/apple/cmd.pla,
and the same in cmdjit.pla, sossys.pla, sossysjit.pla, a1cmd.pla,
c64/cmd.pla, and plvm's `load_mod` in cmdsys.c) reads the whole module
file into the heap, the body and then the RLD and ESD after it,
applies the RLD, and only then frees the RLD and ESD. So a module's
load peak is its whole file.

**Why it's a problem.** It's the blocker for running ccish on a real
64K Apple II now. cccomp's module file is 44,017 bytes (2026-11-09),
12,266 of it RLD and ESD, and free memory on such a machine is under
about 40K (approach.md 18.6, never measured), so cccomp can't load at
all, even though its body (31,751 bytes) would fit. The RLD is a
run-once table: each entry is applied once and thrown away, so it
never needs to be in memory all at once.

**Will cccomp fit with it?** Probably, but only just. Measured
2026-11-09, cccomp needs, while it runs: its body (31,751), the
loader's def table, kept (1,465: 5 bytes for each of 293 functions),
the zero-filled tables it puts on the heap at start (2,388), a frame
stack of at least about 1,560 (more for deeply nested expressions),
and one 1K ProDOS I/O buffer for its second open file (the first uses
ProDOS's buffer at $0800): about 38.2K in all, plus FILEIO's 2,011
resident bytes, which CMD doesn't load until a module needs it. Free
memory at the PLASMA 2.20 prompt measures 41,171 bytes (item 15), so
streaming is necessary, and likely enough, but the margin is about 1K. The next step after
streaming is to measure free memory on an emulator or a real 64K
machine; items 13 to 15 below would add margin if it's needed. (ccish
has shrunk cccomp's code from 28,321 to 25,854 bytes in 2026-11-08 and
-09, and will keep trying.)

ccish's doc/measure.txt now gives each program's needs, loading
(today and streamed) and running, from what `loadmod` allocates; it
counts each I/O buffer beyond the first as 1,280 bytes, since
`iobufalloc` page-aligns each one (`heapallocalign(..., 8, ...)`
skips 1 to 256 bytes). As of 2026-11-11, cccomp has 980 bytes to
spare running, and ccpp 942.

**Also: no check that the RLD fits.** `loadmod` allocates heap only
for the body (`allocheap(modsize)`), then reads the rest of the file,
the RLD and ESD, into the memory after it, 4K at a time, and frees it
after. I don't see a check there that it fits below the frame stack,
so a module whose body fits but whose whole file doesn't may overwrite
the stack rather than fail with an error. Streaming fixes this too;
until then, a check (say, an `allocheap` of the rest, released after)
would make such a load fail cleanly.

**The fix.** Read the body into the heap, then stream the RLD four
bytes at a time from the open file, applying each entry as it
arrives. Definitions ($02) come first, so the def table is complete
before the first fixup, and internal fixups ($8x) can be applied at
once. The catch is the external fixups ($9x): they need the ESD, which
comes after the RLD. Three options:

- **A.** Move the ESD before the RLD: the simplest streaming loop, but
  a format change for every writer and loader.
- **B.** Keep the format, and defer the external fixups: keep each
  one's place and ESD index in a small list (46 entries, 138 bytes,
  when cccomp was last counted), then resolve them from the ESD,
  which follows the RLD's terminator. No format change; only the
  loaders change.
- **C.** Both: a new magic number (the loaders already dispatch on
  $6502) for a layout with the ESD first, streamed; the old format
  loads as today. This makes the transition easiest, and it's the one
  I'd ask for.

cccomp's load peak would drop from about 44K to about 32K. While the
format is open, item 12's zero-run encoding of the data segment would
fit in well.

## 2. plvm: exit with the module's status

**What it does.** plvm's `main` (src/vmsrc/cmdsys.c) calls
`load_mod()`, which runs the module's init and returns init's
result, then ignores it and returns 0. So plvm always exits 0, even
when the module failed.

**Why it's a problem.** A script or test on the host can't tell a
failing program from a working one. PLASMA already has the right
convention: a negative init result is an error (cmd.pla's `loadmod`
sets `perr = -result`, and CMD prints `ERR:$xx`), and $2000 and up
mean "keep the module". ccish's init returns `-(status & 255)` of
`main`'s result. To get it out of plvm today, ccish runs every test
through a driver module, `tools/ccstat.pla`, which runs the program
with `cmdsys:modexec` and prints a marker line that `tools/ccrun`
strips again. That works, but it puts a marker into stdout, needs a
wrapper and xplasm and acme in the build, and can't report an exit
from a crash.

**The fix.** About three lines in `main`: keep `load_mod`'s result
and, if it's negative, return its negation (`-r & 255`); else 0.
Modules that return a negative init result are failing already, so
nothing that works today changes. With that, `plvm prog; echo $?`
just works, and ccstat becomes unnecessary.

## 3. VM opcodes for unsigned comparisons (for speed)

This one is mainly about speed, not size: it saves only 2 bytes per
comparison, but makes each about three times faster, and lets
unsigned and pointer loops use the fast counted-loop ops.

**What's there.** PLASMA's ordered comparisons (`ISLT`, `ISGT`,
`ISLE`, `ISGE`, and the counted-loop branches like `INCBRLE`) are all
signed. CMDSYS provides `isult`, `isugt`, `isule` and `isuge`, but
each use is a `CALL` into a native routine, several times slower than
a 1-byte opcode (`ISLT` is about 44 cycles, the call about 123), plus
3 bytes for the call.

**Why it's a problem.** C needs unsigned comparisons for every
`unsigned` comparison and every pointer `<`, `>`, `<=` and `>=`.
ccish calls the CMDSYS routines (since 2026-11-09: that beat its
earlier sign-bit flip, which took 5 to 9 bytes and about 131 to 218
cycles; approach.md and OBJECT.md, object version 8). An opcode would
make each 1 byte instead of 3, a small saving (cccomp has few such
comparisons), but about three times faster. Loops are where speed
matters most, and today only `int` loops can use the counted-loop
ops; unsigned and pointer loops can't.

**The fix.** Four opcodes, `ISULT`, `ISUGT`, `ISULE` and `ISUGE`,
giving `$FFFF` for true like the existing comparisons, in the free
range `$C6` to `$FE`. Related, and worth doing at the same time:
step-and-branch-if-not-equal ops (`INCBRNE` and friends), which would
let every `!=` loop keep its count on the eval stack, since `!=` has
no sign, and unsigned counted-loop ops (`INCBRULE` and friends). The
cost: every VM (plvm01.s, plvm02.s, plvm03.s, plvm802.s, plvmc64.s,
plvm.c, plvmjit02.s/plvmjit03.s) and the JITs (jitcore.pla,
jit16core.pla, cleanjit.pla) need the new ops, and an old VM crashes
on an unknown opcode instead of rejecting it, so a module using them
should depend on a marker module (approach.md issue 21).

## 4. plvm: reject an import it can't resolve

**What it does.** `load_mod` resolves each imported symbol with
`lookup_sym`, which returns 0 for an unknown name, and plvm patches
the 0 in without a word. A FILEIO test that imported `perr` (which
plvm's FILEIO doesn't export) read zero page instead.

**Why it's a problem.** Every mistake in a module's imports turns into
wrong behavior at run time, far from its cause. The Apple II's loader
already does the right thing: cmd.pla's `lookupextern` prints
`?NAME` and sets `perr` to $81.

**The fix.** In plvm, do what cmd.pla does: report the name and fail
the load (`pfail`).

## 5. plvm: stop on an eval stack overflow or underflow

**What it does.** Before each op, plvm's interpreter (`vm_interp` in
src/vmsrc/plvm.c) checks the eval stack pointer. If it's out of range
it prints `Eval stack over/underflow! - $addr: $op [n]` to stdout,
resets the stack to empty, and keeps going; the `exit(-1)` after it is
commented out.

**Why it's a problem.** Unbalanced code (a compiler bug, usually) runs
on with a wrong stack, and the process still exits normally, so a
test that doesn't compare the output passes (ccish's gen tests check
the listing and the exit status, not the output). The message also lands in the
program's stdout rather than stderr. The 6502 VMs don't check at all
(the stack is in zero page, and an overflow silently corrupts it), so
plvm is the one place such a bug can be caught.

**The fix.** Print the message to stderr and stop with a nonzero
status, the commented-out `exit(-1)`, so the bug can't be missed.

## 6. plvm: a way to unwind (exit from deep in a call)

**What it does.** plvm runs each bytecode call as a C recursion
(`CALL` -> `M6502_exec` -> `vm_interp`). PLASMA's except/throw
(src/libsrc/longjmp.pla) restores the 6502 stack and zero page, which
can't unwind those C frames: a test of it crashed plvm ("stack
smashing detected").

**Why it's a problem.** C's `exit()` from inside a nested call (and
later `longjmp`) needs to unwind. ccish's passes call `exit` on every
fatal error, from deep in the call tree (diag.c's `stop()`). On plvm,
ccish can only stop the VM with a 6502 `BRK`, which plvm reports as an
error and exits 255; lib/exit.c does that under plvm, after printing
ccstat's marker so the status still gets out. Nothing written in a
module can do better: each caller's IP is a local of a C frame
(`vm_interp`), out of the program's reach (checked 2026-11-08).

**The fix.** Either make except/throw work in plvm (natively, with C's
`setjmp`/`longjmp` around `vm_interp`), or add a native exit that
flushes output and exits with a status. The first is more general and
keeps PLASMA programs portable between plvm and the 6502 VMs. With 2
and a native exit, exit's plvm path would shrink to one call; with
except/throw working, to nothing.

The 6502 VMs don't need a change: there the return IPs are on the
6502 stack, the eval stack is in zero page (X), and the frames are
IFP and PP, so a 6502 save and restore, like except/throw, can unwind
(doc/bootstrap-test.md, step 14). A program can tell plvm apart by
`MACHID`, which plvm sets to $08 (an Apple 1). plvm's Apple 1 mode
(`initApple1` in a1cffa.c) runs the real 6502 VM, plvm01.s, under
emulation, with no C recursion, so it may be a place to test such code
without an Apple II; untried.

## 7. plvm: FILEIO fidelity

plvm's native FILEIO (src/vmsrc/sysio.c) works, but differs from the
Apple II's in ways that hide bugs (all tested 2026-11-07):

- `create`, `destroy` and `rename` return the stale `errno`, so they
  report an error (2, after an earlier failed open) even when they
  succeed. They should return 0 on success.
- `create` of an existing file truncates it; ProDOS returns an error
  ($47). Code that works under plvm can fail on the real machine.
- FILEIO's `perr` isn't exported (see 4); plvm keeps the error in
  CMDSYS's `syserr` instead.
- `geteof`, `seteof`, `getfileinfo`, `setfileinfo`, `getpfx`,
  `setpfx` and `online` only print "unimplemented".

## 8. plvm: CMDSYS's `call` and `syscall`

plvm exports `CALL` as `syscall6502`, an empty stub that prints
"SYSCALL6502 unimplemented!". On an Apple II, CMDSYS's
`call(addr, a, x, y, status)` runs 6502 code at `addr`, and
`syscall(cmd, params)` makes a ProDOS MLI call. plvm's `SYSCALL`
jumps to $FF10, which has no handler. So `CALL` should run 6502 code
as the Apple II's does, and `SYSCALL` should either emulate the
common MLI calls on host files or fail with a clear message.

## 9. plvm and the 6502 VMs disagree

doc/Bytecodes.md ("Significant discrepancies") has the details; checked
against the source 2026-11-07:

- `DIVMOD`'s two results come in opposite orders: the remainder is on
  top on the 6502 VMs, the quotient on plvm (ccish never emits it).
- `NATV` isn't implemented in plvm (an illegal opcode there).
- `SEL` searches linearly on plvm but needs a sorted table on the
  6502, so code that only runs under plvm won't catch an unsorted
  one.
- Division by zero depends on plvm's host, since plvm uses C's `/`
  and `%`, which are undefined for a zero divisor: it traps (SIGFPE)
  on x86, and on ARM64 gives `a/0` = 0 and `a%0` = `a`. The 6502 VMs
  give garbage and go on. Defining a result in plvm would make it
  the same on every host.

`NOT` no longer differs: plvm gives $FFFF (-1), like the 6502 VMs
(plvm.c, `TOS = TOS ? 0 : -1`). Bytecodes.md's table agrees, but its
closing sentence still says to avoid `NOT` for kept values.

These are best fixed in plvm, since the 6502 VMs define the
behavior on real machines.

## 10. Smaller ones

- **xplasm `-O`** folds constant comparisons and `not` to 1 instead
  of $FFFF (a bug in the cross-compiler's optimizer).
- **plvm's module name is a 16-character symbol.** The name on plvm's
  command line becomes a DCI string, so a path is cut off
  (`/home/dwheeler/projects/...` loads `/home/dwheeler/pr`). You
  have to run a module from its own directory, by a short name.
  Accepting a full host path for the first module would be easier.

## 11. plvm: a stderr route

**What it does.** A PLASMA program has one output, the console
(CMDSYS's `cout` and `puts`), and plvm writes it to the host's
stdout. So ccish's library sends `stderr` to the console too.

**Why it's a problem.** Under plvm, a pass's error messages are mixed
into its stdout: a script can't separate them, and a test can't check
that a successful run printed no errors except by its output as a
whole.

**The fix.** A second output in plvm that goes to the host's stderr:
for example, a CMDSYS or CONIO entry, or a reserved FILEIO reference
number. On the Apple II it would just be the screen, so programs stay
portable.

## 12. The loaders: zero runs in the data segment (smaller files)

**What's there.** A module file holds its data segment byte for byte,
and most of it is zeros: the zero-filled globals and tables too small
for cclink to move to the heap (approach.md, "Large zero-filled data on
the heap").

**Why it's a problem.** Disk space and load time. A 140K floppy holds
the PLASMA system, ccish's four programs, its library and its message
file; reading from it is slow. Measured 2026-11-09, storing each run of
5 or more zeros as a count would save:

| Program | Module file | Data segment | Zero bytes | File saved |
|---|---|---|---|---|
| cccomp | 44,017 | 4,799 | 4,444 (92%) | about 4,300 (10%) |
| ccpp | 27,386 | 4,877 | 3,698 (75%) | about 3,500 (13%) |
| ccgen | 33,624 | 4,411 | 3,140 (71%) | about 2,700 (8%) |
| cclink | 16,595 | 2,326 | 1,953 (83%) | about 1,850 (11%) |

About 12.4K across the four. Without streaming (item 1) it would also
lower the load peak by as much (cccomp's to about 39.7K: not enough
alone); with streaming, it doesn't change memory, since the data takes
the same memory once loaded. It would also let cclink keep more zero
tables in the file instead of on the heap, whose pointer indirection
costs code.

**The fix.** Store the data segment as chunks: a 2-byte length then
that many bytes, or a marker length ($FFFF) then a count of zeros.
Only the data segment: the bytecode has many short zero runs, where
the framing would cost more than it saves. A free bit in the header's
`sysflags` word marks a file that uses it, so old files load as they
do now. cclink would write it (src/link.c).

**ccish's own programs** get this from XLOAD's images (doc/xload.md,
section 3), whose data segments are stored as runs (2026-11-12). The
request stands for REL modules in general.

## 13. CMD: run `main` after init, so startup code can be freed

**What's there.** CMD frees a module's init function after it runs
(unless init asks to keep it). But a ccish program runs all of `main`
inside init: cclink's init calls `main` and returns its status
(approach.md decision 36), so nothing is freed until the program is
done.

**Why it's a problem.** Code that runs once at startup (argument
parsing, finding the message file, opening the files, filling
tables) stays in memory for the whole run, while cccomp's margin on a
64K machine is under 2K (item 1). I'd guess 0.5K to 1.5K for cccomp;
measuring which functions run only at startup would tell.

**The fix.** A way for init to return and have CMD then call another
entry (say, an export named `main`, or a flag in the header), after
freeing init's code. cclink would put the startup-only code in the init
segment. The exit status would come from that entry instead of init.

## 14. VM: call bytecode directly (no def entry per function)

**What's there.** Every function in a module gets a 5-byte def entry
(`JSR $03D6` and the address, src/vmsrc/apple/cmd.pla `adddef`), kept
for the life of the module, because `CALL` enters machine code, and
the entry is what jumps into the interpreter.

**Why it's a problem.** cccomp has 293 functions: 1,465 bytes of def
entries, held the whole run, and every call goes through the extra
`JSR`. Nearly all are called only from inside cccomp itself.

**The fix.** A call op that enters a bytecode function directly (say
`CALLB addr`), so that only exported and address-taken functions need
def entries: about 1.4K for cccomp, and faster calls. It's a VM change
like item 3's (every VM and JIT), with the same marker-module caution.

## 15. Documentation: a map of a 64K Apple II's free memory

**What's there.** No one had measured what a 64K Apple II has free
for a module once ProDOS, the VM, CMD, the zero page, the stacks and
the text screen are in; "under about 40K" was an estimate
(approach.md 18.6, E1). *Measured 2026-11-09:* 41,171 bytes
(`MEM FREE:$A0D3`, which CMD prints at its prompt when there's no
AUTORUN), booting PLASMA 2.20's own disk (`PLEIADES 64K VM v2.20`) on
MAME's enhanced IIe with 64K and no 80-column card (approach.md,
"Free memory, measured"; ccish's tools/a2screen). That's the heap
available at the prompt, before any module or FILEIO is loaded. A map
of what lies where (and in the language card) is still to do.

**Why it's a problem.** cccomp's fit (item 1) turns on it. A map
would also show any holes the loader could use for small fixed things,
such as the def table or a second ProDOS I/O buffer (FILEIO already
uses ProDOS's buffer at $0800 for the first open file).

**The fix.** Measure it on an emulator or a real machine (the
available heap after CMD starts, and what lies in the language card),
and record it in PLASMA's docs. ccish will need an emulator for this
anyway, once streaming exists.

## 16. plvm: lookupsym traces every lookup

**What's there.** plvm's CMDSYS `lookupsym` (vmsrc/cmdsys.c,
`syslookuptbl`) prints `LOOKUPSYM: name => $addr` on stdout for every
call: its `if (trace)` is commented out.

**Why it's a problem.** A program's output gets a line per lookup.
ccish's XLOAD (doc/xload.md) looks up each import of the program it
loads, so its tests must filter those lines out (tests/imgrun.sh).

**The fix.** Restore the `if (trace)`.

