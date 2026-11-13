# The compiler bootstrap test

The compiler compiles itself, twice, under the VM, and the two results
must be byte-for-byte the same. This file is the working record of
where that stands, what we recommend doing next and why, and what
blocks it. (Revised 2026-11-07 after reviewing the first attempt,
commits 7d986620 and 42cc5148.)

## Summary

**Status (2026-11-08): the bootstrap passes.** `make bootstrap` builds
the four programs three times (on the host, then twice under plvm),
and every object and module is the same each time (step 4, below).
What's left before the Apple II is steps 14 and 15: an `exit()` that
unwinds (deferred: under plvm, today's stand-in reports failure well
enough), and the machine itself, with cccomp fitting its memory (B7).

The first attempt got useful data (the per-file sweep and the
capacity blockers below), but several of its choices work against
where ccish is going. In short, we recommend:

1. **Bootstrap the four pass programs, one source file per
   invocation**, not the all-in-one `ccish` driver. That's the Apple II
   design already (AGENTS.md, approach.md 18.5), it's the only one that
   can fit 64K, and it makes the in-process multi-file mode (and B1)
   unnecessary.
2. **`#include <stdio.h>` everywhere.** gcc uses the system headers;
   ccish, on the host or under the VM, uses ccish's own headers, found
   through an include directory we control.
3. **Prefix the CMDSYS names with `__`** (`__puts`, `__memset`, ...),
   so the standard names are free for a real C library with C's
   semantics.
4. **Write a real ccish C library** in its own directory, not
   compiler-specific glue in `src/vmsys.c`.
5. **Do file I/O through PLASMA's FILEIO module.** plvm already
   implements it natively; the first attempt's raw-MLI path doesn't
   work anywhere (see "Verified VM facts").
6. **Implement function pointers** (wanted anyway) and imports from
   other modules (asm labels and `#pragma plasma import`, not a naming
   convention), so the library can call FILEIO's table.

Items 1, 3, 5 and 6 were decided in discussion on 2026-11-07
(approach.md issue 22, decision 35); 7's first part is done
(decision 36).
7. **Get an exit status out:** `main`'s return value through the
   module's init and a driver module to the host (done), and an
   `exit()` that unwinds (not yet: the library leaves `exit` out, and
   the passes link a plvm-only stand-in; R8).
8. **Drop the six-argument `printf`;** use a few fixed-arity output
   functions until varargs exist (approach.md already decided this).

## Verified VM facts (2026-11-07, from the source)

These correct what the first attempt assumed. I checked them by
reading the code, and then ran small PLASMA modules (xplasm, acme)
under plvm that import FILEIO: open, read, create, write, close,
destroy and newline all worked on host files.

- **plvm implements FILEIO.** `export_sysio` (src/vmsrc/sysio.c)
  registers a `FILEIO` symbol whose 20-entry table matches `t_fileio`
  (src/inc/fileio.plh). open, close, read, write, create, destroy,
  rename, newline, getmark and setmark work on host files; getpfx,
  setpfx, get/setfileinfo, get/seteof, online and the block calls only
  print "unimplemented". On an Apple II, the same table comes from
  src/libsrc/apple/fileio.pla, which wraps ProDOS MLI. So a module
  that uses FILEIO works on both.
- **plvm's loader satisfies a dependency on FILEIO.** `load_mod`
  (src/vmsrc/cmdsys.c) skips loading a dependency whose symbol already
  exists, so a module listing `FILEIO` binds to the built-in one under
  plvm and loads `FILEIO` from the system directory on an Apple II.
- **plvm does not emulate raw MLI.** `syscall6502`, behind the
  `CALL` export, is an empty stub, and the `SYSCALL` export jumps to
  $FF10, which has no callback.
- **The first attempt called the wrong CMDSYS function.** CMDSYS
  `call(addr, a, x, y, status)` runs machine code at `addr` (on an
  Apple II, `call(0xC8, ...)` jumps into zero page). The MLI entry is
  `syscall(cmd, params)`. plvm muddles this by exporting
  `syscall6502` as `CALL`.
- **The command numbers are ProDOS MLI's,** and $C7 is GET_PREFIX,
  not an "OPENIN". There's one OPEN ($C8); you CREATE ($C0) a file
  before writing it, and DESTROY or SET_EOF to truncate one. MLI paths
  are length-prefixed, and OPEN needs a 1K page-aligned I/O buffer,
  which FILEIO manages for you.
- **plvm FILEIO quirks (tested):** `open` of a missing file returns
  0; `open` uses `O_RDWR` without `O_CREAT`, so you create first (as
  on ProDOS); `create`, `destroy` and `rename` return the stale
  `errno` even on success (2 after an earlier failed open); `create`
  of an existing file truncates it, where ProDOS returns an error
  ($47); writing over a longer file doesn't truncate it (`AB` over
  `0123456789` gave `AB23456789`); LF becomes CR only in newline
  mode. So the library should judge success by `open`'s refnum and the
  byte counts, not by `create`'s or `destroy`'s result, ccpp should
  accept CR, LF, or CR LF as a line end, and `fopen(path, "wb")`
  should do destroy, create, open.
- **plvm doesn't export FILEIO's `perr`.** fileio.plh imports `byte
  perr`; plvm has no `PERR` symbol, and its loader resolves an unknown
  import to address 0 without complaint, so the test read 0 after a
  failed open. plvm keeps the error in CMDSYS's `syserr` (t_cmdsys
  offset 16) instead. Portable code shouldn't depend on either.
- **The CMDSYS write slot is empty on the Apple II.** cmd.pla's
  t_cmdsys has `@open, @close, @read, 0`, so `sysopen` and friends
  aren't a substitute for FILEIO.
- **The command line includes the module name** in plvm (main in
  src/vmsrc/cmdsys.c joins argv from the module name on), and
  PLASMA's own `argFirst`/`argNext` skip it the same way. Its address
  is in `t_cmdsys.cmdline`; don't hardcode $01FF.
- **plvm discards init's result.** `main` in cmdsys.c ignores
  `load_mod`'s return and returns 0. On the Apple II, a negative init
  result sets `perr` to its negation, and CMD reports it as an error.
- **PLASMA's except/throw (src/libsrc/longjmp.pla) is 6502 code** that
  saves and restores the hardware stack and zero page. That works on a
  6502 VM, but I believe it can't unwind plvm, whose interpreter
  recurses on the C stack (`vm_interp`).

## Recommendations, and why

### R1. Bootstrap the four passes, one file per invocation

On the Apple II, ccish is four programs (ccpp, cccomp, ccgen, ccrel),
each with nearly all of memory. The all-in-one `ccish` is a host
convenience; it can't fit (cccomp alone is 42,743 bytes of module file
today). So the bootstrap builds the modules `ccpp`, `cccomp`, `ccgen`
and `ccrel` from the same groups of files the Makefile uses (COMMON,
PP, NAM, FRONT, BACK, LINK), and each stage is a batch of short
invocations:

```
for each source file f of the compiler:
    ccpp f.c        -> f.i, f.nam
    cccomp f        -> f.ir
    ccgen f         -> f.obj, f.syms
for each program p in ccpp, cccomp, ccgen, ccrel:
    ccrel -o p @p.objs    (p's objects, then the library's)
```

- Stage 1 runs that with the host-built passes; stage 2 runs it under
  plvm with stage 1's modules; stage 3 with stage 2's. The test: stage
  2's and stage 3's modules (and, as a bonus, their objects) are
  byte-for-byte identical. The script is the batch file; only the
  executor changes.
- Why: it's what you'd do on the real machine, each process starts
  with clean memory (so the passes need no per-file reset, and B1
  drops out), and a stage 2 versus stage 3 difference shows up in the
  first object that differs, file by file.
- So `src/main.c` should go back to its single-file form (commit
  7d986620 made it compile N files in one process, which breaks on
  file 2, and added `input_base`/`link_base`). ccish doesn't need a
  `-c` for the bootstrap, since ccpp/cccomp/ccgen already stop at the
  object; add one later only if it helps on the host.
- tools/measure.py already does most of this (it compiles every
  source with the passes and links each program with the library
  objects it needs). The bootstrap script should share that
  preparation rather than grow a second copy. The small build now
  compiles `src/` unchanged: the `short` casts are masks, and the
  struct `?:` is an `if`. The full build's `printf` is the one
  workaround left.

### R2. Standard includes through a directory we control (done)

Done in phase 5: src/pp.c adds the default directory after the `-I`
ones, `CCISH_INC` if the build defines it (the Makefile does, as
lib/include's absolute path), else `<syspath>ccinc` from `<plasma.h>`'s
`cmdsys`; lowercase, since plvm's file system is case sensitive. A
self-compiled ccpp under plvm finds `<string.h>` in `sys/ccinc`. Its
name file then names the headers by their `sys/ccinc` paths, so it
isn't byte for byte the host ccpp's; modules don't hold file names.
The src/ includes were already `<...>`.

You should be able to write `#include <stdio.h>` and build the same
file with gcc, with ccish on the host, and with ccish on the Apple II.

- **gcc:** the system headers, untouched; the host build of the
  compiler stays an ordinary C89 program.
- **ccish on the host:** ccpp searches `-I` directories for `<...>`
  (src/pp.c `do_include`), then a default directory, compiled in as
  the absolute path of the library's include directory (say
  `ccish/lib/include`). A `-I` placed first can override it.
- **ccish under plvm or on the Apple II:** the default is derived from
  CMDSYS's `syspath`, e.g. `<syspath>CCINC/`. plvm sets `syspath` to
  the current directory plus `sys/`, so the stage script makes
  `sys/ccinc` (and `sys/cclib`, R4) in each stage directory, a copy of
  or link to the library. That's the "control the library directory"
  trick, and it's the same layout an Apple II disk would use. ProDOS
  names (15 characters, letters, digits and periods, no case) are fine
  for `STDIO.H` and the like.
- So the 32 include lines the first attempt changed to quotes go back
  to angle brackets, `src/std` moves into the library, and the flat
  copies beside the sources go away. It also removes the hazard of a
  file named `stdio.h` landing next to a source.

### R3. CMDSYS names get a `__` prefix (done, decision 35)

Today the CMDSYS names are fixed ids 1 to NCMDSYS with PLASMA's
semantics (`cmdsys_sigs`, src/parse.c): `puts` takes a length-prefixed
string and adds no newline, `getc()` takes no stream, `memset` fills a
16-bit pattern, `strcpy` copies PLASMA strings. A program can only
override one with a definition of exactly the same type, which is why
the first attempt needed `#define getc getc_f` and why `memset` with a
nonzero byte silently differs from C.

- Rename the fixed ids `__putc`, `__puts`, `__getc`, `__memset`,
  `__strcpy`, `__call`, `__syscall`, `__heapalloc`, and so on. The
  linker strips `__` when it writes the import (`PUTS`). Names
  beginning with `__` are reserved to the implementation (C11
  7.1.3), so no conforming program collides with them.
- The standard names are then ordinary functions the library defines
  with C's semantics. Where a CMDSYS routine is faster, the library
  calls it: `memset(p, c, n)` can be `__memset(p, c | c << 8, n)`.
- The tests' `inc/ptest.h` can `#define puts __puts` and so on, so the
  suite changes little. The guide's "same type" override rule (2.3,
  5.3) goes away.

### R4. A ccish C library, in its own directory (done)

Done in phase 5, one object per function (the smallest links), built
by the Makefile into `tests/out/lib/`. The guide's 11.2 lists them;
the xrun tests xstring and xstdio check them against the host's C
library.

`src/vmsys.c` mixes string functions, stdio, `printf`, `exit` and the
command line in one compiler-specific file. Instead:

- `lib/include/` for the headers and `lib/` for the sources, compiled
  by ccish into objects the stage script (and later, users) link.
- Small objects (roughly one per function or group), because ccrel
  links whole objects and every byte counts in 64K. Later, ccrel could
  search a library directory and link only the objects that define
  something still undefined, as a Unix `ld` does with an archive;
  until then the link lists name them.
- Only what the compiler needs first (approach.md 18.5 lists it):
  `string.h`, the stdio file calls, `exit`, and the startup code
  (R6). `malloc` follows approach.md 12's sketch when something needs
  it.

### R5. File I/O through FILEIO (done)

Done in phase 5, unbuffered (bio.c buffers for the compiler). A `FILE
*` is FILEIO's reference number, or negative for the console, so
there's no table of streams. Found while testing: plvm's FILEIO create
and destroy return `errno` even when they succeed, a stale value, so
`fopen` ignores them (its open fails if they did) and `remove` can't
report a missing file; and plvm doesn't implement `geteof`.

The library's stdio sits on FILEIO's table: `open`, `read`, `write`,
`close`, `create`, `destroy`, plus `newline` if we ever want text
mode. It converts C strings to length-prefixed paths, keeps a small
table of open files, and gives `fopen(path, "wb")` its C meaning with
destroy, create, open. That works under plvm today and on an Apple II
with the stock FILEIO module.

We don't recommend implementing raw MLI in plvm's `syscall6502` for
this. It would work (fileio.pla shows exactly which calls and blocks),
and it would fix PLFORTH's `SYSCALL`, but it's emulation-only code
the real machine doesn't need, and C code that builds MLI blocks is
bigger than calls through FILEIO.

### R6. Function pointers and module imports (decided, decision 35)

Calling FILEIO means loading a word from its table and calling
through it. PLASMA has the op (ICAL); ccish needs the two pieces
below. approach.md issue 22 has the options and why a naming
convention (`__MODULE_ENTRY`) lost.

- **Function pointers**, which approach.md 18.5 found the compiler
  itself doesn't use, but which we want anyway. Two PLASMA rules shape
  them: a call must pass exactly the callee's parameter count, and a
  function returns exactly its declared number of values (#0, #1).
  So a pointer's type must carry its parameter count and whether it
  returns a value, and a call through it is checked like a direct
  call. FILEIO's `geteof` and `getmark` return two values (#2), which
  C can't express; the library leaves them out.
- **Imports from other modules.** ccish modules import only CMDSYS
  functions today (guide 2.3). An `extern` that no object defines,
  declared after `#pragma plasma import MODULE`, becomes an import of
  a PLASMA symbol, and the module gets a dependency on MODULE if any
  such extern is used. The symbol is the asm label if there is one,
  else the uppercased C name; names that collide once uppercased and
  cut to 16 characters are an error. So the library's headers say
  `extern const struct t_fileio fileio __asm__("FILEIO");` and call
  `fileio.open(p)`, and `extern const struct t_cmdsys cmdsys
  __asm__("CMDSYS");` reaches `cmdline`, `syspath` and `syserr`
  (plvm exports the `CMDSYS` symbol; it needs no dependency).
- plvm's loader resolves an import it can't find to address 0,
  silently (`lookup_sym`), so a mistake in these declarations shows
  up as a mystery at run time. Rejecting it is a request in
  doc/plasma-requests.md; until then, test each new import.

### R7. Startup and `main(argc, argv)` (done)

Done in phase 5: cccomp accepts `main` with 0 or 2 parameters, and
when it has 2, ccrel's init calls `__start` (lib/start.c) instead,
which splits the command line in place (its text moved down over the
length byte, so the last NUL fits) into at most 24 words. Without the
start object, the link fails with "undefined reference to
'__start'". measure.py's `main_args` rename and tools/libstub.c are
gone.

The passes take command lines, and ccish's `main` takes no parameters
today (guide 5.1), hence measure.py's former `main_args` rename and the first
attempt's `cmd_argv` with hardcoded addresses. Instead, the library's
startup (an object the link includes, or code ccrel's init emits)
splits `cmdsys.cmdline` (R6) in a static buffer, skips the module name,
and calls `main(argc, argv)`. ccish then accepts `int main(int, char
**)` as well as `int main(void)`. The passes' sources need no
`__CCISH__` branches.

### R8. The exit status (done for `main`, decision 36)

Without it, a failing pass under plvm looks like a successful one.
Done without changing PLASMA (approach.md issue 22 has the options):

1. **`main`'s return value becomes init's result:** ccrel's init
   returns `-(status & 255)`, PLASMA's error convention, never
   positive (which would mean "keep the module"). An Apple II's CMD
   shows it as `ERR:$xx`.
2. **The host gets it through a driver:** `plvm ccstat prog args`
   runs `prog` with exactly `prog args` as its command line and prints
   a marker with init's result; `tools/ccrun` strips the marker and
   exits with the status. A module run directly is unchanged. The
   run, gen and xrun tests use it. (Making plvm itself exit with the
   status is in doc/plasma-requests.md.)
3. **`exit()` from deep in a call** is still open. The compiler calls
   it 5 times, all on failure: each pass's `usage()`, and diag.c's
   `stop()`, which every error reaches, often from deep in the call
   tree, so the compiler can't do without it. Tested: longjmp.pla's
   except/throw crashes plvm. A real `exit()` must restore the 6502
   stack and the VM's frame and evaluation stacks to where init called
   `main`, close the open files, and return the status as init's
   result: except/throw, or a small 6502 routine of ccish's own,
   called through a function pointer. Decided (2026-11-08): the
   library doesn't define `exit` until then (`<stdlib.h>` declares it,
   so a call is an undefined reference at link time), and ccish's own
   passes link `tools/plvmexit.c`, a plvm-only stand-in that prints
   ccstat's marker and runs a BRK (a function pointer to a 0 byte;
   plvm doesn't implement `__call`). plvm exits at a BRK, so nothing
   needs unwinding there; on an Apple II, a BRK drops into the monitor,
   which is why it isn't the library's. **Deferred** (2026-11-08,
   step 14): plvmexit.c stays, since it's enough to tell failure from
   success under plvm, and a real `exit` waits for the Apple II.
   **Done** (2026-11-12): lib/exit.c replaces tools/plvmexit.c. On an
   Apple II it closes the files and unwinds, with the 6502 routines
   step 14 describes, saving just before main (ccrel's init calls
   `__exitsave` when exit is linked), and init returns the status;
   under plvm it does what plvmexit.c did. Checked on MAME's 64K IIe:
   a pass's error prints its message, then `ERR:$01` and CMD's prompt,
   and the next command runs. It costs each pass 207 bytes (exit.c's
   94 of code and 123 of data, against plvmexit.c's 37, and 12 in
   init).
   **Smaller** (2026-11-12, decided in discussion): init runs the save
   routine itself, right before main's call. ICAL and CALL push the
   same frame on both Apple VMs (CALLX and ICALX too), so the save
   routine sees the stack pointer main runs at, and main's call frame
   stays intact while main runs; exit's routine puts back the stack
   pointer, X, IFP and PP, with the status on the evaluation stack,
   and its RTS returns from main's call. No stack bytes are copied, and
   `__exitsave` is gone. exit no longer closes files (CMD closes them
   all after a `+` command), and its plvm path is back in
   tools/plvmexit.c, which tools/cclink links for plvm runs (`-a`, as
   measure.py uses, links lib/exit.c's). exit.c is now 11 bytes of code
   and 69 of data, and init 4 more: 160 bytes less in each pass.
   **One exit** (2026-11-13, doc/linking.md 5.5): one link must give a
   module that runs on plvm and the Apple, so lib/exit.c's exit works
   under plvm too, and tools/plvmexit.c and cclink's `-a` are gone.
   Init sets TMP ($E7) itself before running the save routine, which
   plvm's ICAL doesn't, so the routine runs harmlessly on plvm's
   emulated 6502. exit tells plvm apart by $F1, the opcode of the Apple
   VMs' zero-page fetch loop (0 under plvm), and there prints ccstat's
   marker, now ^A and the number (no "exit ", no newline), and runs a
   BRK, a 0 byte in the save routine. 23 bytes more in each pass:
   ccpp and cccomp have 740 and 724 to spare running.

### R9. No six-argument `printf` (done)

The first attempt made `printf` take exactly six arguments, padded
every call with zeros, and silenced gcc with `-Wno-format-extra-args`.
That's a nonstandard `printf` that's hard to keep honest, and
approach.md already decided that until varargs, "the runtime use[s]
fixed-argument output functions instead of `printf`". The compiler
already had the pattern (`num_text()` in diag.c, `out_num()` in
lower.c for `-i`), so emit.c's 24 calls (the `-d` dump) now use
lower.c's `out_num()` and `fputs()`; the gen tests confirm the output
is unchanged. link.c's 23 calls stay: they're all under
`CCISH_LISTING`, which the small build (the one the bootstrap
compiles) leaves out. A real `printf` waits for varargs (approach.md,
`...` with a prototype).

### R10. The command-line budget

plvm's command line holds 127 characters, and an Apple II's isn't
bigger. The per-file pass lines are short. A link line naming a
program's objects plus the library's can approach the limit, so give
ccrel a list file (`ccrel -o cccomp @cccomp.objs`, one name per line)
rather than one-letter output names. The source and object names stay
real names, and the list files are the same at every stage. **Done**
(2026-11-08): `@file` in ccrel.c, read into a 512-byte buffer, one
name per line, CR or LF ending a line. ccrel's `bases[]` shrank from
256 entries to `MAX_OBJS` (32, now in link.h), so its module file
grew only 374 bytes (code +180, data +68, strings +10). Under plvm,
the self-compiled ccrel links cccomp's 30 objects from a list (187
bytes) to the same module as the host's. The list files are PROG.objs,
not .lst, which is a listing everywhere else in ccish (tools/out's,
the run tests'). It's GCC's `@file` syntax but not its format: only
names, a whole line each, with no options, quoting or white space
splitting.

## What to keep, and what to undo, from the first attempt

| Change | Recommendation |
|---|---|
| Makefile: `BOOTSTRAP_VM`, the bootstrap stamp kept out of `SPEC`, `bootstrap` and `full-test` targets, `clean` | Keep |
| src/irin.c: `mod = 0` for modulo 65536 | Keep; a real 16-bit fix |
| The per-file sweep and blockers B2 to B7 | Keep (below) |
| src/main.c: N files in one process | Revert (R1) |
| tests/special/bootstrap.sh: one invocation, one-letter sources | Rewrite (R1, R10) |
| `-DSMALL` alias in src/build.h | Drop; the short lines don't need it |
| Quoted std includes in 32 places, `src/std` | Revert to `<...>`; headers move to the library (R2) |
| src/vmsys.c | Replace with the library (R4 to R7) |
| `#define getc getc_f`, `memset`/`memcpy` as CMDSYS imports | Superseded by R3 |
| Six-argument `printf`, `-Wno-format-extra-args` | Replace (R9) |
| `call(cmd, n, block, 0, 0)` with "SOS" numbers | Wrong on both VMs; replace (R5) |

## The per-file sweep (from the first attempt)

Each source compiled alone by the host ccish, small build. An
"undefined" row is expected for one object: the name is in another.

```
main     link: fatal 222, too many names shared (B5; main isn't in R1's programs)
diag     link: undefined fopen (expected alone)
bio      link: undefined fopen (expected alone)
ppname   link: undefined strlen (expected alone)
pplex    link: undefined pp_macro_flag (expected alone)
ppmac    link: undefined pp_id_spelling (expected alone)
ppif     link: undefined pp_next_raw (expected alone)
ppout    link: undefined error (expected alone)
pp       link: undefined pp_out (expected alone)
nam      link: undefined fopen (expected alone)
tokin    link: undefined nam_open (expected alone)
name     link: undefined fatal (expected alone)
type     link: undefined error (expected alone)
parse    ccpp: fatal 244, too many macros (B2)
pexpr    ccpp: fatal 244 (B2)
pstmt    ccpp: fatal 244 (B2)
ptop     ccpp: fatal 244 (B2)
irout    link: fatal 222 (B5)
irin     fatal 28, too many variables (B3)
lower    link: undefined num_text (expected alone)
peep     link: undefined emit_put (expected alone)
emit     fatal 138, too many case labels (B4)
helper   link: undefined lower_op (expected alone)
link     link: undefined strcmp (expected alone)
vmsys    compiles and links its own object clean
```

measure.py's BIG_LIMITS path already works around B2 to B4 to measure
sizes; the bootstrap needs them fixed for real.

## The blockers

**B2. ccpp's macro limits.** `MAX_MACROS 72` and `MAX_MACRO_BYTES
512` (src/ppmac.c, sized for parse.c at 62 macros and 274 bytes) are
exceeded by parse, pexpr, pstmt and ptop (at IR_REFS_LABEL, ir.h:151).
These are per-translation limits, so one file at a time doesn't help.
ccpp has headroom (26,049-byte module file); measure the cost of
raising them. **Done** (2026-11-08): dropping includes the parser's
files didn't need left ptop.c at 88 macros and 608 bytes (about 20 of
the macros are include guards), so they're 104 and 704 now. That costs
ccpp 480 bytes (288 of data, 192 zero-filled), and cccomp nothing.

**B3. `counted_loop` (src/irin.c) overruns MAX_VARS.** irin.c's
globals plus its locals exceed cccomp's 79 visible variables. Options:
split the function, or raise the limit; cccomp is the program that
doesn't fit, so splitting is probably cheaper. Measured 2026-11-08:
it's not one function. irin.c has about 70 file-scope variables (32 of
them the loop machinery's `lp_*`, `cl_*` and `fo_*`), so nearly any
function with more than five of its own overflows. The same day, link.c
needed 49 types (`MAX_TYPES` 40), mostly distinct array types.
**Done** (2026-11-08): the loop machinery moved to src/irloop.c,
which irin.c reaches through `loop_take()` and `loop_reset()`, and
which reads irin.c's swap buffer (src/irloop.h). The most visible
variables is now 76 (link.c). It costs ccgen 50 bytes of code and 66
of module file. link.c has no clean split (every part reads the
shared-name table, `fbuf` and the object offsets), so `MAX_TYPES` is
54, 10% over its 49: 70 bytes of cccomp's data.

**B4. emit.c's `mnem()` overruns cccomp's limits.** The first sweep
stopped at its switch's case labels (MAX_CASES, 36 open at once); on
2026-11-07 tools/measure.py stops at its returns ("error 179: too many
returns in mnem", emit.c:353), so ccgen can't be measured. A table of
names instead of a 70-way switch of returns is probably smaller as
well; otherwise raise the limits, at a cost in cccomp's data. **Done**
(2026-11-07): `mnems[]`, a table of the mnemonics indexed by opcode
($20 to $BE, 160 bytes of pointers), replaced the switch, and the
listings are unchanged. emit.c now compiles under ccish (code 2,835,
data 608, strings 641), and ccgen links for the first time: code
20,571, data 2,911, strings 1,444, a 34,076-byte module file (8,747 of
it RLD and ESD), 199 functions (995 bytes of def table), and 18,292
bytes of zero-filled tables on the heap. 12,288 of those are emit.c's
`lbuf[]`, kept only for `-d`'s listing, in both builds. So `-d` is now
in the full build only (CCISH_LISTING, as ccrel's `-l` is; decided
2026-11-07). The small ccgen: code 19,590 (-981), data 2,745 (-166),
strings 820 (-624), a 31,496-byte module file (-2,580), and 6,004 bytes
of heap tables (-12,288). Its body, def table and heap tables come to
about 30.1K, which fits the roughly 41.7K free with room for its frame
stack and file buffers (approach.md, issue 24, has the budget).

**B5. ccrel's shared-name table.** MAX_GSYMS 163 and MAX_GNAMES 1427
(src/link.c) are exceeded by irout's summary, because a header's
declarations count even when nothing uses them. R1 removes main.c
from the bootstrap, but cccomp's link still carries its whole API.
Options: raise the limits (about 13 bytes per name), or write slimmer
summaries (only names an object defines or references, as a C linker
keeps). ccrel has the most headroom (14,200-byte module file).
**Done** (2026-11-08): cccomp's link, with the library, needs 271
names and 2,716 bytes, so they're 307 (the first prime 10% above) and
3,000. That costs ccrel 3,589 bytes, nearly all zero-filled tables on
the heap.

**MAX_IDS and MAX_MEMBERS** (cccomp) were over too, in 2026-11-08's
measure: pstmt.c saw 210 enum constants and typedef names (203), and
pp.c 36 struct members (32), 29 of them `<plasma.h>`'s. Fixed in the
headers, at no cost: `<fileio.h>` now holds FILEIO's table apart from
`<plasma.h>`'s `cmdsys`, and obj.h's object format moved to obj.fmt.h
(the record tags of the stream and the IR file, and the helpers) and
obj.sym.h (the relocations and symbol flags), so the parser's files
don't see them. The most is now 200 identifiers (irin.c, which sees
mop.h's 83 machine ops) and 27 members.

**B6. File I/O (done).** Not a VM gap after all: plvm has FILEIO. The
gate was ccish's side: R3 to R7 (function pointers, data imports, the
library, startup), all done.

**B7. The 64K budget.** cccomp's module file (42,743 bytes) doesn't
fit an Apple II's free memory yet (AGENTS.md); under plvm, with no
ProDOS or screen, it may load anyway, so stage 2 may run on the host
before cccomp fits the real machine. Track every limit B2 to B5 raises
against each program's size with tools/measure.py, and the library's
size, which is new code in every program.

**B1 (dropped).** The passes don't reset their tables between files in
one process. R1 never asks them to. If we ever want one process to
compile several files on the host, it's a separate feature.

## Implementation order

Each step is tested and committed on its own. The order follows the
dependencies, gets better test feedback early, and puts the biggest
size risk (function pointers in cccomp) where we learn its cost before
building on it.

**Phase 0: cleanups (independent, small).**
1. Keep `mod = 0` in src/irin.c (already in 7d986620).
2. Revert main.c's multi-input mode, the `-DSMALL` alias, and the
   six-argument `printf` padding (with `-Wno-format-extra-args`); put
   the std includes back to `<...>`. Retire src/vmsys.c, src/std and
   the old bootstrap.sh (the library and the new script replace
   them); until the new script exists, `make bootstrap` says so and
   fails.
3. Replace `printf` in emit.c with `out_num()` and `fputs()` (R9).
   Done; ccgen became measurable once B4 was fixed.

**Phase 1: test feedback (done, decision 36).**
4. ccrel's init returns `main`'s status, and the ccstat driver and
   `tools/ccrun` hand it to the host; the run, gen and xrun tests
   check it. The plvm changes that would help (exiting with the
   status itself, rejecting an unresolved import) are requests in
   doc/plasma-requests.md, not part of this plan.

**Phase 2: names (done).**
5. The `__` prefix on the CMDSYS fixed ids (R3): ppname.c's fixed
   names, link.c (the ESD drops the `__`; its init calls `__memset`
   and the rest), inc/ptest.h (maps `puti` and friends to `__puti`),
   the tests, and the docs. Cost: ccpp +61 bytes (the longer fixed
   names), cccomp +3, ccrel +50.

**Phase 3: function pointers (done; approach.md, "Function pointer
design").**
6. Declarations in C's full syntax (done), `&f` and plain `f` as
   values and calls through a pointer (done: ICAL; a function type is
   only its return type, so the arguments aren't checked), function
   addresses in initializers (done). Cross-check with xrun tests against gcc
   and clang. Cost: cccomp 1,771 bytes of code in all (step 1 1,196,
   step 2 534, step 3 41).

**Phase 4: imports from other modules (done, 2026-11-07; approach.md
decision 35).**
7. The asm label and `#pragma plasma import` in cccomp; dependency
   names and imports beyond CMDSYS in ccrel (OBJECT.md version bump
   if the .syms format changes). Test with run tests that read
   `cmdsys` fields and call a CONIO or FILEIO entry. (This step
   doesn't need step 6 for data imports, so it could go first; the
   table calls need both.) Done: `.syms` version 7, name file version
   2; tests/run/imports.c reads `cmdsys` and calls FILEIO's `open`,
   `read` and `close` under plvm. Cost: cccomp +721 bytes of code and
   +96 of data, ccrel +640 of code, ccpp +165, ccgen +59.

**Phase 5: the library and startup (done, 2026-11-08).**
8. `lib/include` and the default include directory: compiled in on
   the host, `<syspath>ccinc` under the VM (R2).
9. Startup and `main(argc, argv)` from `cmdsys.cmdline` (R7).
10. The C library (R4, R5): string.h, then stdio over FILEIO
    (`fopen`, `fread`, `fwrite`, `fclose`, `remove`, `getc`, `putc`,
    `fputs`), and no `exit` (R8 part 3), with xrun tests against the
    host's library and tests/special/lib.sh under plvm. Measured with
    the library linked in (each program links only the objects it
    needs): cccomp's library is 760 bytes of code and 179 of data
    (the 128-byte path buffer, the 50-byte `argv`), its module file
    49,051 bytes (+951); ccpp's own code grew 55 bytes (the
    `<syspath>ccinc` branch) and its library is 858 bytes of code;
    ccgen's 824; ccrel's 822.

**Phase 6: the bootstrap (done, 2026-11-08).**
11. ccrel's `@list` file (R10), then rewrite tests/special/bootstrap.sh
    for R1 and R2, sharing measure.py's preparation.
12. B2 to B5, each measured against B7.
13. Run the stages; diagnose a stage 2 versus stage 3 difference with
    the kept directories, file by file (`-i` and `-d` dumps line up).

The order agreed on 2026-11-08, as five steps:

- **Step 5, first: the ccgen miscompile (done, 15008d8c).** From the
  same IR, the self-compiled ccgen wrote a different type.obj than the
  host's. `mop_const` and the const AND peephole used range tests on
  an `int` holding a 16-bit value, and from $8000 up that's negative
  on the Apple II. Now the host and self-compiled ccgen write identical
  objects for every file in src/. The check, a throwaway script: for
  each tools/out/src/*.c, the host ccpp (or tools/out/big/ccpp) and
  cccomp (or big/cccomp) with `-DCCISH_SMALL` and `-k`, then the host
  ccgen and `tools/ccrun vccgen` (measure.py's tools/out/ccgen, with
  ccish.msg beside it) on copies of the same .ir and .nam; cmp the
  .obj and .syms. The bootstrap test (step 4) should subsume it.
- **Step 1: compile src/ unchanged (done, a29036cb).** The `short`
  casts are masks, the struct `?:` is an `if`, and measure.py's only
  workaround is the full build's `printf`. (Its old `short`/`long`
  regex also rewrote pplex.c's keyword strings.)
- **Step 2: fit the tables (done, 85b8a376 and the commit after).**
  B2 and B5 are raised; `MAX_MEMBERS` and `MAX_IDS` are fixed in the headers
  (`<fileio.h>`, obj.fmt.h, obj.sym.h). Left over, cccomp's:
  `MAX_VARS` (irin.c needs 83, the limit is 79) and `MAX_TYPES`
  (link.c needs 49, the limit is 40). **Decided 2026-11-08 (option
  1): split the files rather than raise cccomp's limits.** Move
  irin.c's loop machinery (`lp_*`, `cl_*`, `fo_*` and their functions,
  about 32 of irin.c's roughly 70 file-scope variables; around lines
  980-1400) to a file of its own, and find a similar split of link.c
  (its types are mostly distinct array types: one per table size). If
  link.c has no clean split, raise `MAX_TYPES` alone (about 5 bytes of
  cccomp per type, so about 70). Measure each with `python3 -I -S
  tools/measure.py`: "over a limit" must go, and cccomp's size must
  not grow. irin.c's identifiers are 200 of `MAX_IDS` 203, the
  tightest left. Done as B3 says: irin.c is split and `MAX_TYPES`
  raised, so every file in src/ now fits cccomp's tables (the closest
  are `MAX_IDS`, 200 of 203 in irin.c, and `MAX_VARS`,
  76 of 79 in link.c).
- **Step 3: ccrel's `@list` file (R10; done).** `ccrel -o cccomp
  @cccomp.objs`, one object name per line, read into a static buffer
  in ccrel only; cccomp's link names 30 objects, over the 127-character
  command line.
- **Step 4: tests/special/bootstrap.sh.** Stage 1: the host passes
  compile src/ (small build) and the library, and link the four
  programs, finding each program's library objects as measure.py does
  (one per undefined reference; see its `lib_object`), and write the
  .lst files every stage shares. Stages 2 and 3: the same under plvm
  through tools/ccrun, each in its own directory with `sys/ccinc`
  (lib/include) and ccish.msg (diag looks beside argv[0]). The test:
  stage 2's and stage 3's modules and objects identical, and stage
  1's objects equal to stage 2's too (that's the check that caught
  ccgen); on a difference, name the first object that differs and
  keep the directories. **Done** (2026-11-08), with one script for
  the library objects (decided in discussion): tools/cclink links a
  program, adds the library object each undefined reference calls
  for, and writes PROG.objs, the objects linked. measure.py's own loop
  (`lib_object`) is gone; it calls cclink too, and its report is
  unchanged. The Makefile gives the script each program's sources
  (`CCPP_SRC` and the rest, which the program rules use too) and the
  library's, so no list is written by hand. The stages are in
  tests/out/boot/1 to 3, removed when they match. All three stages'
  49 objects (27 of src/, 22 of the library) and four modules are the
  same: the modules are 28,011 (ccpp), 49,121 (cccomp), 32,678 (ccgen)
  and 16,405 (ccrel) bytes, as measure.py reports. `make bootstrap`
  takes about 36 seconds.

Each step: discuss any design change first, then test (`make`, which
runs the test suite), measure, update the docs, and commit with
explicit paths. Every pass runs under plvm in well under a second per
file, so the bootstrap should be quick.

**Before the Apple II.**
14. `exit()` that unwinds (R8 part 3), in the library, replacing
    tools/plvmexit.c (**done** 2026-11-12: lib/exit.c, R8); and a
    stderr route. A successful stage never
    calls `exit`, so the bootstrap under plvm doesn't wait for it, but
    the passes can't run on an Apple II without it. **Deferred**
    (decided in discussion, 2026-11-08): under plvm we only need to
    tell failure from success, and today's stand-in does that (a
    failing pass gives tools/ccrun status 1, and bootstrap.sh stops
    and names it). The better fix is in plvm (doc/plasma-requests.md,
    6 and 11), and it's easier to ask for once ccish is something
    PLASMA wants. What we found, for when we come back to it:
    - On plvm, nothing in a module can unwind. Each bytecode `CALL`
      re-enters `vm_interp` through `M6502_exec` in C, and each
      caller's IP is a C local (src/vmsrc/plvm.c), which is also why
      longjmp.pla's except/throw smashes the stack there.
    - On the Apple II it can be done in ccish: plvm02.s's `CALL` and
      `ICAL` push the return IP on the 6502 stack, the eval stack is
      zero page indexed by X, and the frames are IFP and PP. So a small
      6502 routine (bytes called through a function pointer, as
      plvmexit.c's BRK is) can save S, X, IFP, PP and the stack bytes
      above S around `main`, and `exit` can restore them, close the
      files fopen opened, and return the status as `main` would.
    - A program can tell which: plvm sets `MACHID` to $08 (an Apple 1).
      So the library's `exit` could unwind on a 6502 VM and fall back
      to plvmexit.c's way on plvm.
    - There's no Apple II emulator here. plvm's Apple 1 mode
      (`initApple1`, src/vmsrc/a1cffa.c) runs the real 6502 VM,
      plvm01.s, under emulation, with no C recursion, so it may be a
      test bed for that routine; untried.
15. The Apple II itself: CR line ends in ccpp, ProDOS file names
    (`ccinc` is lowercase today), a disk layout matching R2's `sys/`
    directories, and cccomp fitting in free memory (B7).
