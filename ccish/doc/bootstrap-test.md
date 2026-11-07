# The compiler bootstrap test

The compiler compiles itself, twice, under the VM, and the two results
must be byte-for-byte the same module. This file is the working record
of where that stands, what is built, and what blocks it.

## The stages

```
stage 1   the seed: this source's host build (the Makefile's ccish,
          built by $(CC)) compiles the compiler's source into a
          module for the VM, bootstrap/s1/cc
stage 2   s1/cc, run under the VM ($(BOOTSTRAP_VM), default the
          PLVMBIN, ../src/plvm), compiles the same source into
          bootstrap/s2/cc
stage 3   s2/cc does it again into bootstrap/s3/cc
test      s2/cc and s3/cc are byte-for-byte identical
```

`make bootstrap` runs it; `make full-test` runs the regular suite and
then this. It is deliberately not in the regular `make test` suite
(it is slow, and the stages after 1 depend on VM work that is not
done). The Makefile entry is a spec stamp,
`tests/out/spec/bootstrap.stamp`, like the other spec tests, but
`tests/spec/bootstrap.sh` is filtered out of `SPEC`.

## What exists

- `Makefile`: `BOOTSTRAP_VM ?= $(PLVMBIN)` (the macro for the 6502
  emulator used by the later stages); the bootstrap stamp rule, which
  passes `BOOTSTRAP_VM` to the script; the phony targets `bootstrap`
  and `full-test`; `clean` removes the `bootstrap` working directory.
- `tests/spec/bootstrap.sh`: the stage logic. It copies the compiler's
  source into each stage directory under one-letter names, `a.c`
  through `y.c` (link order), plus the project headers and the std
  headers, flat beside the sources. It runs the stages and compares
  them, keeping every stage directory (with its `-k` intermediate
  files and its IR/machine-op dumps) for diagnosis.
- The stage command lines must fit the VM's command-line buffer, 128
  bytes holding at most 127 characters (plvm's `char cmdline[128]` in
  vmsrc/cmdsys.c, joined with a space after each word, length stored
  at `CMDLINE_STR` 0x01FF, text at `CMDLINE_BUF` 0x0200). The stage
  line is `-DSMALL -i -d -o cc a.c b.c ... y.c`, 118 characters.
  The one-letter names exist for this, and `-DSMALL` is a short alias
  for `-DCCISH_SMALL` added to src/build.h for the same reason.
- `ccish` now orchestrates several source files in one run
  (src/main.c, `run()`): one input needs no `-o`; more inputs require
  `-o`, each input gets the passes under its own base name, and the
  final pass links them all. The VM build's `main` takes no argv:
  `cmd_argv` (src/vmsys.c) splits the VM's command line into one.
- `src/std/`: the headers a build by ccish itself includes, `stdio.h`,
  `stdlib.h`, `string.h`, `stddef.h`, declaring only what the
  compiler's source uses. They live in `src/std`, not `inc`, so the
  host oracle builds (`make xref`, which compiles the test programs
  with `-Iinc`) never see them. The sources include them quoted
  (`#include "stdio.h"`), which the preprocessor looks for beside the
  including file first (src/pp.c `do_include`); the host builds fall
  through to the system headers.
- `src/vmsys.c`: the library a build by ccish itself needs, present
  only under `__CCISH__`: the string functions, stdio, `exit`, and
  `cmd_argv`.

## Decisions already made (recorded so they are not relitigated)

- **printf takes six arguments.** ccish has no variadic arguments
  (guide section 9.2), and calls are arity-checked even against
  old-style declarations. So `src/std/stdio.h` declares
  `printf(fmt, a1..a5)`, every call in the source passes exactly
  six (padded with zeros), and the host build compiles the padding
  quietly via `-Wno-format-extra-args` in CFLAGS. The VM's printf
  (vmsys.c) formats `%d`, `%+d`, `%s`, `%02X`, `%04X`, `%%`, the
  full set the source uses.
- **The standard headers use quoted includes**, for the stage
  directories (above).
- **`getc` is renamed.** `getc` is a CMDSYS fixed id with a different
  signature (src/parse.c `cmdsys_sigs`); `src/std/stdio.h` does
  `#define getc getc_f`, and vmsys.c provides `getc_f(FILE *)`.
- **`memset` and `memcpy` are CMDSYS imports, not local functions.**
  Their declarations in `src/std/string.h` must match `cmdsys_sigs`
  (`vpii`, `vpPi`), and a call in a module is an import. vmsys.c
  does not define them.
- **`mod = 0` means modulo 65536** in `between_rewrite` (src/irin.c):
  the old `mod = 65536` constant does not fit in a 16-bit int, and
  `(0 - w) & 0xFFFF` is the same mask. Host output is unchanged; the
  code-generation tests still pass.
- **The VM file calls follow the PLASMA source's own protocol**
  (libsrc/apple/fileio.pla): through CMDSYS's 6502 syscall natv,
  exported as `CALL`, with the parameter block's byte 0 the count
  and its words the values. OPENIN `$C7` / OPENOUT `$C8` take
  `[3, path, iobuf, 0]` and return the refnum in the block's byte 5
  (as `a2open` reads it); READ `$CA` / WRITE `$CB` take
  `[4, refnum, buf, len, count]` and return the byte count in word 4
  (as `a23read`/`a2write` read it); CLOSE `$CC` takes `[1, refnum]`;
  DESTROY `$C1` takes `[1, path]` (as `a23destroy`). The C side
  calls `call(cmd, count, block, 0, 0)`; `cmdsys_sigs` gives `call`
  five int parameters, the two beyond the block's address unused.

## Where it stands

`make bootstrap` runs all the way through stage 1's file loop and
stops at the first real blocker below (B1): file 2's front end
collides with file 1's state, `obj.h:27: error 27: redeclaration of
'REC_DATA'`. The regular suite is green (379 tests) with all of the
above in place.

A per-file sweep (each source compiled alone by the host ccish,
small build) shows where every file stands. The `undefined
reference` rows are expected for a single object: those names live in
other objects (the vmsys library, the other passes) and resolve only
in the full link.

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

## The blockers, in order

**B1. The passes do not reset between files (in-process multi-file).**
Each pass is a program of its own (approach.md "B3"); ccish's driver
runs them in one process, and `compile_file` / `parse_program` (src/
ptop.c) start the next file with all of the previous file's state:
the function table (nfuncs, fn_*), the variable table (nvars,
nglobals, var_*), the type and tag tables, the label and goto tables,
the held buffers, the name table, the preprocessor's file table and
**macro table** (a #define in file 1 would leak into file 2, which
C forbids), and the back end's own tables. A second file therefore
"redeclares" the first file's symbols. The fix is an explicit
per-file reset of every pass's state, called between files (the
single-file case works only because process start gives zeroed
state). This is the top task: audit each of pp*.c, tokin.c, name.c,
type.c, parse.c, pexpr.c, pstmt.c, ptop.c, nam.c, irout.c, irin.c,
lower.c, peep.c, emit.c, helper.c for per-file state, and write one
reset per pass.

**B2. ccpp's macro limits are sized for one file of the compiler,
not four.** `MAX_MACROS 72` and `MAX_MACRO_BYTES 512` (src/ppmac.c,
commented as sized at 62 macros / 274 bytes for parse.c) are
exceeded by the parse/pexpr/pstmt/ptop translations (the error hits
at the IR_REFS_LABEL definition, ir.h:151). Raising them costs
preprocessor table memory in the module's 64K; measure first.

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
already exceeded by the main.c translation alone: its headers
declare most of the compiler's API, so its symbol summary carries
177 names. The full 25-object link needs more still. Options:
raise the limits (g_name, g_hash, g_count, g_flags, g_def, g_desc,
gnames: roughly 13 bytes per name of data) against the module's
64K, or emit slimmer summaries (referenced or defined symbols only,
as a C linker keeps) so a header's declarations stop counting.

**B6. The VM has no file I/O yet (blocks stages 2 and 3).**
plvm's syscall6502, the body behind CMDSYS's CALL export, prints
"SYSCALL6502 unimplemented!". Until it exists, a stage module cannot
read its sources or write its output module. The file calls the
module makes are the SOS protocol recorded above. A way to provide
them, in plvm (PLASMA-side work, not ccish-side): an option that
maps these calls onto ordinary host files in a directory of the
emulator, i.e. a minimal ProDOS of open/read/write/close over a
host directory, with refnums into a small table. That is where the
user's suggested "plvm option for basic ProDOS" lands.

**B7. The 64K budget (RLD size).** Even once the stages run, the
small-build module must fit the VM's 64K: code, data (including the
linker's tables if B5 raises them), the name table, and the compiler
buffers. Measure the stage 1 module's size when B1 is past, and
track every limit raised in B2/B3/B4/B5 against the budget.

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

1. B1: the per-pass resets, and a multi-file test in the regular
   suite (two small .c files linked into one module) so the reset
   stays tested.
2. B2, B3, B4: the capacity limits, each measured against the 64K
   budget (B7) before raising.
3. B5: the shared-name table, or slimmer summaries.
4. Rerun the stage 1 link of all 25 objects; measure the module.
5. B6: the plvm file-call option (PLASMA-side), then stages 2 and 3.
6. Diagnose any stage 2 vs stage 3 difference with the kept
   directories: the IR dumps (-i) and machine-op dumps (-d) line up
   file for file.
