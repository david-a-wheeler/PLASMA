# The compiler bootstrap test

The compiler compiles itself, twice, under the VM, and the two results
must be byte-for-byte the same module. This file is the working record
of where that stands, what is built, and what blocks it.

## The stages

The VM simulates one execution at a time, and on a real system (say
ProDOS) a batch file would invoke the compiler per file. So each
stage is a sequence of invocations, not one big run, and the stage
script plays the batch file's role. The same sequence of commands
runs at every stage; only the executor changes.

```
for each source file f, in link order:
    cc -c -o <base> f.c         compile f into base.obj and base.syms
ccrel -o cc <bases...>          link the objects into the module cc
```

- **Stage 1, the seed.** The host builds, `$(CC)`'s ccish and ccrel,
  run the sequence over the compiler's source, producing the stage 1
  modules: `bootstrap/s1/cc`, the compiler (the whole source,
  one file per invocation), and `bootstrap/s1/ccrel`, the linker
  (the ccrel subset of the source: ccrel, link, diag, bio, vmsys).
- **Stage 2.** plvm (`$(BOOTSTRAP_VM)`, default the PLVMBIN,
  `../src/plvm`) runs `s1/cc` per file and `s1/ccrel` for the link,
  producing `s2/cc` and `s2/ccrel`.
- **Stage 3.** The same with the stage 2 modules, producing
  `s3/cc` and `s3/ccrel`.
- **The test.** `s2/cc` and `s3/cc` are byte-for-byte identical
  (and, as a bonus check, `s2/ccrel` and `s3/ccrel`).

`make bootstrap` runs it; `make full-test` runs the regular suite
and then this. It is deliberately not in the regular `make test`
suite (it is slow, and the stages after 1 depend on VM work that is
not done). The Makefile entry is a spec stamp,
`tests/out/spec/bootstrap.stamp`, like the other spec tests, but
`tests/spec/bootstrap.sh` is filtered out of `SPEC`.

**The command-line budget.** plvm's command line holds at most 127
characters (the 128-byte `cmdline` buffer in vmsrc/cmdsys.c, a space
after each word, length at `CMDLINE_STR` 0x01FF, text at
`CMDLINE_BUF` 0x0200). The per-file lines are short. The link line
carries every base, so the bases are one-letter, `a` through `y`:
`ccrel -o cc a b ... y` is 56 characters. The sources keep their
real names; the one-letter names are output bases only.

## What exists, and what the batch design changes

The Makefile constructs stand: `BOOTSTRAP_VM ?= $(PLVMBIN)`, the
bootstrap stamp (which passes `BOOTSTRAP_VM` to the script), the
`bootstrap` and `full-test` targets, and `clean` removing the
working directory.

The committed script and driver follow the older one-big-invocation
design (all 25 files on one command line, one in-process run) and
need reworking to the batched stages above:

- `tests/spec/bootstrap.sh`: rebuild as the per-file loop plus the
  ccrel link, per stage, for both source sets (the compiler and the
  linker). Real source names; one-letter output bases.
- `src/main.c`: the current `run()` compiles N files in one process
  and links them. That in-process multi-file mode is broken without
  the per-pass resets (B1, demoted below) and is no longer needed:
  replace it with single-file compilation plus a compile-only mode
  (the `-c` of the stage lines; exact spelling to decide), keeping
  the test suite's single-file-to-module behavior. The
  `input_base`/`link_base` tables go away with it.
- `-DSMALL` (src/build.h): added to fit the old 127-character
  mega-line. The per-file lines fit with `-DCCISH_SMALL`, so the
  alias is a candidate for removal.
- The one-letter *source* names (`a.c` through `y.c`) in the stage
  directories: superseded by real source names plus one-letter
  output bases.

The rest of the committed changes are unaffected by the batch design
and stand: the `src/std` headers, `src/vmsys.c`, the six-argument
printf, `getc_f`, `memset`/`memcpy` as CMDSYS imports, `mod = 0` in
src/irin.c, and the CFLAGS flag.

## Decisions already made (recorded so they are not relitigated)

- **printf takes six arguments.** ccish has no variadic arguments
  (guide section 9.2), and calls are arity-checked even against
  old-style declarations. So `src/std/stdio.h` declares
  `printf(fmt, a1..a5)`, every call in the source passes exactly
  six (padded with zeros), and the host build compiles the padding
  quietly via `-Wno-format-extra-args` in CFLAGS. The VM's printf
  (vmsys.c) formats `%d`, `%+d`, `%s`, `%02X`, `%04X`, `%%`, the
  full set the source uses.
- **`getc` is renamed.** `getc` is a CMDSYS fixed id with a
  different signature (src/parse.c `cmdsys_sigs`); `src/std/stdio.h`
  does `#define getc getc_f`, and vmsys.c provides `getc_f(FILE *)`.
- **`memset` and `memcpy` are CMDSYS imports, not local functions.**
  Their declarations in `src/std/string.h` must match
  `cmdsys_sigs` (`vpii`, `vpPi`), and a call in a module is an
  import. vmsys.c does not define them.
- **`mod = 0` means modulo 65536** in `between_rewrite`
  (src/irin.c): the old `mod = 65536` constant does not fit in a
  16-bit int, and `(0 - w) & 0xFFFF` is the same mask. Host output
  is unchanged; the code-generation tests still pass.
- **The VM file calls follow the PLASMA source's own protocol**
  (libsrc/apple/fileio.pla): through CMDSYS's 6502 syscall natv,
  exported as `CALL`, with the parameter block's byte 0 the count
  and its words the values. OPENIN `$C7` / OPENOUT `$C8` take
  `[3, path, iobuf, 0]` and return the refnum in the block's byte 5
  (as `a2open` reads it); READ `$CA` / WRITE `$CB` take
  `[4, refnum, buf, len, count]` and return the byte count in word 4
  (as `a23read`/`a2write` read it); CLOSE `$CC` takes `[1, refnum]`;
  DESTROY `$C1` takes `[1, path]` (as `a23destroy`). The C side
  calls `call(cmd, count, block, 0, 0)`; `cmdsys_sigs` gives
  `call` five int parameters, the two beyond the block's address
  unused.
- **`src/std` is not `inc`.** The std headers for a ccish build
  live in `src/std`, so the host oracle builds (`make xref`, which
  compiles the test programs with `-Iinc`) never see them.

## Where it stands

The regular suite is green (379 tests) with all of the above in
place. The committed script's one-big-invocation stage 1 stops at
file 2's front end colliding with file 1's state (`obj.h:27:
error 27: redeclaration of 'REC_DATA'`, B1 as originally framed);
under the batched stages that failure does not occur, because each
invocation starts in zeroed memory. The per-file sweep below is the
state of the sources themselves, and it holds under either design.

A per-file sweep (each source compiled alone by the host ccish,
small build) shows where every file stands. The `undefined
reference` rows are expected for a single object: those names live
in other objects (the vmsys library, the other passes) and resolve
only in the full link.

```
a.c (main)     link: fatal 222, too many names shared (B5)
b.c (diag)     link: undefined fopen (expected alone)
c.c (bio)      link: undefined fopen (expected alone)
d.c (ppname)   link: undefined strlen (expected alone)
e.c (pplex)    link: undefined pp_macro_flag (expected alone)
f.c (ppmac)    link: undefined pp_id_spelling (expected alone)
g.c (ppif)     link: undefined pp_next_raw (expected alone)
h.c (ppout)    link: undefined error (expected alone)
i.c (pp)       link: undefined pp_out (expected alone)
j.c (nam)      link: undefined fopen (expected alone)
k.c (tokin)    link: undefined nam_open (expected alone)
l.c (name)     link: undefined fatal (expected alone)
m.c (type)     link: undefined error (expected alone)
n.c (parse)    ccpp: fatal 244, too many macros (B2)
o.c (pexpr)    ccpp: fatal 244 (B2)
p.c (pstmt)    ccpp: fatal 244 (B2)
q.c (ptop)     ccpp: fatal 244 (B2)
r.c (irout)    link: fatal 222 (B5)
s.c (irin)     fatal 28, too many variables (B3)
t.c (lower)    link: undefined num_text (expected alone)
u.c (peep)     link: undefined emit_put (expected alone)
v.c (emit)     fatal 138, too many case labels (B4)
w.c (helper)   link: undefined lower_op (expected alone)
x.c (link)     link: undefined strcmp (expected alone)
y.c (vmsys)    compiles and links its own object clean
```

## The include style question (to re-investigate)

As committed, the sources' 32 std includes are quoted
(`#include "stdio.h"`), and the stage script copies the std headers
flat beside each stage's sources, found by the "beside the
including file" rule (src/pp.c `do_include`).

The alternative is to keep `#include <stdio.h>` and control the
include path instead, which is possible on both sides:

- the host side: gcc's include list is ours to set in the Makefile
  (`-Iinc` already steers the test oracles); the compiler's own
  host build uses the system headers and is untouched;
- the ccish side: angle-bracket includes search only the `-I`
  directories, and the stage script can pass `-Istd`, with `src/
  std` copied as a subdirectory of each stage rather than flat.

The angle-bracket form is the more conventional style (system-style
headers as `<...>`, local headers as `"...", which is how the
source was before), it keeps one directory from mixing std headers
and project headers, and it removes the shadowing hazard of a file
named `stdio.h` ever landing beside a source. What would change:
the 32 include lines revert, the script copies the directory and
passes `-Istd`, and the Makefile's host builds are untouched.
Verdict: probably better; settle it with the script and main.c
rework.

## The blockers, in order

**B6. The VM has no file I/O yet (the gate).** Every invocation
reads a source and writes objects, so no stage can run until plvm's
syscall6502, the body behind CMDSYS's CALL export, exists (it
prints "SYSCALL6502 unimplemented!"). The file calls the modules
make are the SOS protocol recorded above. A way to provide them, in
plvm (PLASMA-side work, not ccish-side): an option that maps these
calls onto ordinary host files in a directory of the emulator, i.e.
a minimal ProDOS of open/read/write/close over a host directory,
with refnums into a small table.

**B2. ccpp's macro limits are sized for one file of the compiler,
not four.** `MAX_MACROS 72` and `MAX_MACRO_BYTES 512` (src/ppmac.c,
commented as sized at 62 macros / 274 bytes for parse.c) are
exceeded by the parse/pexpr/pstmt/ptop translations (the error hits
at the IR_REFS_LABEL definition, ir.h:151). Batching does not help:
these are per-translation limits. Raising them costs preprocessor
table memory in the module's 64K; measure first.

**B3. `counted_loop` (src/irin.c) overruns MAX_VARS.** The front
end's limit of 79 variables visible at once (src/parse.c, obj.h) is
exceeded by irin.c's globals plus counted_loop's locals. Options:
split the function's locals, or raise the limit with its data cost.

**B4. A switch in src/emit.c overruns MAX_CASES.** 36 case labels
across the switches open at once (src/pstmt.c) is too few for the
opcode dispatch at emit.c:391. Same options: restructure the switch,
or raise the limit.

**B5. The linker's shared-name table is too small for the compiler
itself.** MAX_GSYMS 163 names / MAX_GNAMES 1427 name bytes (src/
link.c, commented as 10% above "linking ccish's" 144 names) is
exceeded by the main.c translation alone: its headers declare most
of the compiler's API, so its symbol summary carries 177 names. The
final ccrel run over all 25 objects needs more still. Options:
raise the limits (g_name, g_hash, g_count, g_flags, g_def, g_desc,
gnames: roughly 13 bytes per name of data) against the module's
64K, or emit slimmer summaries (referenced or defined symbols only,
as a C linker keeps) so a header's declarations stop counting.

**B7. The 64K budget (RLD size).** Once the stages run, the modules
must fit the VM's 64K: code, data (including the linker's tables if
B5 raises them), the name table, and the compiler buffers. Measure
the stage 1 modules' sizes when B6 is past, and track every limit
raised in B2/B3/B4/B5 against the budget. The compiler module
carries the whole source including the linker it does not use in
compile-only mode; dropping link.c from it is a size option to
consider.

**B1. Demoted: the per-pass state reset.** The passes do not reset
between files in one process (compile_file / parse_program, src/
ptop.c, start the next file with the previous file's function,
variable, type, tag, label, name and macro tables still loaded), so
the committed in-process multi-file mode breaks on file 2. The
batched stages sidestep this: each plvm load is a fresh module in
zeroed memory, the way separate processes always gave a clean state.
B1 is now host-side convenience for one-process-multi-file, not in
the bootstrap's path. The broken multi-input path in main.c should
still be removed (or fixed) so the compiler does not carry a known-
broken mode.

## The CMDSYS contract the VM side must honor

For B6, the C side (src/vmsys.c) already assumes, matching the PLASMA
source's fileio.pla:

- `CALL` receives (parameter block address, function number) from
  the 6502 stack (syscall6502's current pull order), returns the
  SOS error code, 0 for success;
- the block's byte 0 is the parameter count, its little-endian words
  the values;
- OPENIN/OPENOUT: `[3, path, iobuf, 0]`, the refnum written into the
  block's byte 5, 0 on failure;
- READ/WRITE: `[4, refnum, buf, len, 0]`, the count of bytes moved
  written into word 4;
- CLOSE: `[1, refnum]`; DESTROY: `[1, path]`.

The C side's `call(cmd, count, block, 0, 0)` (five int arguments,
the CMDSYS signature) passes the function number, the count, and the
block's address; how the VM's CALL receives them is the VM side's
choice to document.

## Next tasks, in order

1. Rework to the batched stages: the script as the per-file loop
   plus the ccrel link, per stage, for both source sets; main.c as
   single-file plus a compile-only mode, dropping the broken
   multi-input loop and its tables; one-letter output bases; drop
   the `-DSMALL` alias if nothing still needs it.
2. Settle the include style question (angle brackets plus `-Istd`
   versus quoted plus flat copies); revert the 32 lines if the
   former wins.
3. B6: the plvm file-call option (PLASMA-side); the gate for stages
   2 and 3.
4. B2, B3, B4: the capacity limits, each measured against the 64K
   budget (B7) before raising.
5. B5: the shared-name table, or slimmer summaries.
6. Run the stages; diagnose any stage 2 versus stage 3 difference
   with the kept directories: the IR dumps (-i) and machine-op
   dumps (-d) line up file for file.
7. Out of scope for the bootstrap: the per-pass resets (B1), if one
   process is ever wanted to compile several files.
