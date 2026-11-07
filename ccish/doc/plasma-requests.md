# Requested changes to PLASMA, outside ccish

ccish works around most of these today. The first doesn't have a
workaround: it's what stops ccish from loading on a real 64K Apple II.
The rest would make ccish (and other PLASMA programs) simpler, faster,
or easier to test. They're in PLASMA's own tools: plvm (src/vmsrc),
the 6502 VMs, and xplasm. The most useful are first. (Started
2026-11-07; the details behind each are in approach.md, issues 21 and
22, and doc/bootstrap-test.md's "Verified VM facts".)

## 1. The loaders: stream the RLD (needed to run ccish on an Apple II)

**What they do.** Every loader (`loadmod` in src/vmsrc/apple/cmd.pla,
and the same in cmdjit.pla, sossys.pla, sossysjit.pla, a1cmd.pla,
c64/cmd.pla, and plvm's `load_mod` in cmdsys.c) reads the whole module
file into the heap, the body and then the RLD and ESD after it,
applies the RLD, and only then frees the RLD and ESD. So a module's
load peak is its whole file.

**Why it's a problem.** It's the blocker for running ccish on a real
64K Apple II now. cccomp's module file is 44,110 bytes (2026-11-07),
about 12.9K of it RLD and ESD, and free memory on such a machine is
under about 40K (approach.md 18.6), so cccomp can't load at all, even
though its body (about 31K) would fit. The RLD is a run-once table:
each entry is applied once and thrown away, so it never needs to be in
memory all at once.

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

cccomp's load peak would drop from about 44K to about 31K. While the
format is open, a zero-run encoding of the data segment (flagged by a
free `sysflags` bit) would also shrink cccomp's file by about 2.7K,
since 85% of its data segment is zeros.

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

## 3. VM opcodes for unsigned comparisons

**What's there.** PLASMA's ordered comparisons (`ISLT`, `ISGT`,
`ISLE`, `ISGE`, and the counted-loop branches like `INCBRLE`) are all
signed. CMDSYS provides `isult`, `isugt`, `isule` and `isuge`, but
each use is a `CALL` into a native routine, several times slower than
a 1-byte opcode (`ISLT` is about 54 cycles), plus 3 bytes for the
call.

**Why it's a problem.** C needs unsigned comparisons for every
`unsigned` comparison and every pointer `<`, `>`, `<=` and `>=`.
ccish avoids the CMDSYS calls by flipping the sign bit of both sides
and comparing signed, which costs 8 bytes and 4 ops per comparison (4
bytes with a constant side; src/irin.c). Loops are where speed
matters most, and today only `int` loops can use the counted-loop ops;
unsigned and pointer loops can't.

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

## 5. plvm: a way to unwind (exit from deep in a call)

**What it does.** plvm runs each bytecode call as a C recursion
(`CALL` -> `M6502_exec` -> `vm_interp`). PLASMA's except/throw
(src/libsrc/longjmp.pla) restores the 6502 stack and zero page, which
can't unwind those C frames: a test of it crashed plvm ("stack
smashing detected").

**Why it's a problem.** C's `exit()` from inside a nested call (and
later `longjmp`) needs to unwind. On plvm, ccish can only stop the VM
with a 6502 `BRK`, which plvm reports as an error and exits 255.

**The fix.** Either make except/throw work in plvm (natively, with C's
`setjmp`/`longjmp` around `vm_interp`), or add a native exit that
flushes output and exits with a status. The first is more general and
keeps PLASMA programs portable between plvm and the 6502 VMs.

## 6. plvm: FILEIO fidelity

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

## 7. plvm: CMDSYS's `call` and `syscall`

plvm exports `CALL` as `syscall6502`, an empty stub that prints
"SYSCALL6502 unimplemented!". On an Apple II, CMDSYS's
`call(addr, a, x, y, status)` runs 6502 code at `addr`, and
`syscall(cmd, params)` makes a ProDOS MLI call. plvm's `SYSCALL`
jumps to $FF10, which has no handler. So `CALL` should run 6502 code
as the Apple II's does, and `SYSCALL` should either emulate the
common MLI calls on host files or fail with a clear message.

## 8. plvm and the 6502 VMs disagree

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

## 9. Smaller ones

- **xplasm `-O`** folds constant comparisons and `not` to 1 instead
  of $FFFF (a bug in the cross-compiler's optimizer).
- **plvm's module name is a 16-character symbol.** The name on plvm's
  command line becomes a DCI string, so a path is cut off
  (`/home/dwheeler/projects/...` loads `/home/dwheeler/pr`). You
  have to run a module from its own directory, by a short name.
  Accepting a full host path for the first module would be easier.
