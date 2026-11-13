# Linking decisions: cclink, -L, lib.map, and the __cmd_ names

The record of one collection of decisions, made in discussion
2026-11-12 and revised after review 2026-11-13, about the final pass
and how names mean things at the link. It's here, in one place, so a
person or an agent can review them together: **approach.md's
decision 46** points here, and `AGENTS.md` ("Where things are written
down") lists this file. Each section gives the decision, the
rationale, the options that lost, and what implementing it touches,
with file and line references for checking (as of 2026-11-13; check
them again, since they move). Section 8 is the list of verified facts
the decisions rest on.

**Status.** Nothing here is implemented. XLOAD, the 128K Apple and
the smaller exit have landed (commits `1eb52f60` and `06e23b19`), so
nothing blocks this. The commit order is section 9's.

## 1. The final pass is renamed `ccrel` to `cclink`

**Decision.** The program, its source file, the Makefile targets,
the tests and the docs: `ccrel` becomes `cclink`.

**Why.** `ccpp`, `cccomp` and `ccgen` are named for what they do;
`ccrel` is the odd one out, named for its output. It writes a REL
module *or* an XLOAD image (`-x`), so the name describes only one of
two outputs. And "linker" is the right word for what it does:
`link.c`'s own header calls it "a static linker" (src/link.c:1). It
resolves the names shared between objects, lays them out and
relocates. With section 5, it also adds the library objects a program
needs, from a directory it's told about.

**No collision.** `tools/cclink`, the shell script that adds the C
library objects today, is deleted before the rename (section 9, step
4), since `-L` does its job. So nothing needs a new name.

**What it touches.** The Makefile (`CCREL_SRC`, the `ccrel` and
`ccrel-small` rules, `PASSES`, clean, the measure, bootstrap and
xload rules, and every other mention); `src/ccrel.c` renamed to
`src/cclink.c`; `tests/special/ccrel.sh` renamed to `cclink.sh`;
`tests/special/xload.sh`, `xload_self.sh` (both run `ccrel-small`),
`lib.sh`, `small.sh` and `bootstrap.sh` (`vccrel`);
`tests/link_test.sh`; tools/measure.py; tools/a2disk (the `CCREL`
file on the CCISH disk becomes `CCLINK`; its header's usage example
too); tools/a2cmd's comments; the usage text in src/ccrel.c; and the
docs: README.md, OBJECT.md, doc/ccish-guide.md, doc/bootstrap-test.md,
doc/xload.md (including its `+XLOAD ... CCREL` examples),
doc/measure.txt, doc/limits.md and AGENTS.md. Grep the whole tree
(not `tests/out`) for `ccrel` and `CCREL` after the change: ccish.msg
has no mention today. approach.md's historical mentions stay;
history isn't rewritten. Decision 46 records the rename.

## 2. Options: no two in one program share a letter

**The rule (user, 2026-11-12, applied to every program 2026-11-13):
no two options of one program may differ only in case.** The reason
is mechanical, not cosmetic: the passes match options with `is_opt()`
(src/diag.c:32), which ignores case, because CMD uppercases the
command line on an Apple II. So in the linker, `-L dir` would be read
as `-l` followed by an input named `dir`. The listing flag must move
**before** `-L` lands.

**Decisions.**

- The linker's `-l` (listing) becomes `-v` ("verbose: print the
  module's listing"). No long options: with limited memory a
  spelled-out form buys nothing.
- The `ccish` driver follows, and its own pairs go too: it has `-i`
  with `-I dir` and `-d` with `-D name` (src/main.c:72-80). The
  driver is host-only and compares case exactly, but one rule for the
  whole family is simpler, and the driver's options mirror the passes'.
  The driver's `-l` becomes `-v`, `-d` becomes `-m` (print the
  **m**achine ops after the peepholes, doc/ccish-guide.md:156) and
  `-i` becomes `-r` (print the I**R**, doc/ccish-guide.md:160).
- ccgen's `-d` and `-i` (src/ccgen.c:38-45) become `-m` and `-r`
  too. They don't clash within ccgen, but the same thing should have
  the same letter across the family.

The letters `-m` and `-r` are a proposal (section 7).

**What it touches.** `src/ccrel.c`'s usage and option loop
(src/ccrel.c:49-57, 99); `src/main.c`'s usage and parse
(src/main.c:72-110); `src/ccgen.c`'s; the Makefile's rules that pass
`-l`, `-d` or `-i` (the run rule's `./ccish -l -o ...`, the gen
rule); tools/listings.py (`ccgen -d`); tools/measure.py (it passes
`-l` to the linker); the special scripts that pass `-l`
(tests/special/ccrel.sh); the guide's option text.

## 3. CMDSYS functions are spelled `__cmd_` + the PLASMA name

**Decision.** The 25 CMDSYS C spellings take the prefix `__cmd_`:
`__cmd_puts`, `__cmd_memcpy`, `__cmd_heapalloc`, and so on. The
fixed ids keep their numbers and order (the token streams are
unchanged, exactly as decision 35's `__` adoption left them); only
the spellings change.

**Why `__` alone is confusing (verified; section 8).** In this tree
the `__` pattern already carries three meanings, all in one fixed-id
table (src/ppname.c:64-71): C's own magic identifiers (`__FILE__`,
`__LINE__`, `__CCISH__`, `__asm__`, `__plasma_import`), the CMDSYS
imports (`__puts`, `__memcpy`, ...), and the C library's private
names (`__start`, `__path`, `__filetype`, `__exitcap`). In the wider
C world `__x` is the established spelling for *the C
implementation's own internal* x (glibc's `__memcpy` is the
library's internal memcpy), so `__memcpy` here reads as the opposite
of what it is. And a bare pattern carries no meaning: "this calls
the VM" exists only in a comment (src/parse.c:199-208).

**The two properties a prefix must have.** *Uniqueness*: no user or
library code may ever be able to define the name, so that a bare
call still means the import without a declaration (the fixed-id
mechanism, src/parse.c:220-226). That comes only from C's reserved
namespaces: C11 7.1.3 reserves identifiers containing a double
underscore, and global names beginning with one. *Meaning* comes
only from a word in the prefix. `__cmd_` has both, and matches the
project's own vocabulary (CMD, CMDSYS). Because the CMD pattern is
now a specific prefix rather than "any double underscore", the C
library's `_` private names (section 4) don't resemble it.

**Options that lost** (discussed 2026-11-12): `__vm_` (unique, two
characters shorter, but "vm" is generic and the module is CMDSYS;
with section 3.1 the length no longer costs ccpp anything);
`plasma_` (most self-documenting, but not reserved: a user program
may legally define `plasma_puts`, so uniqueness is by convention,
not the standard); `vm_` (the same hole); `sys_` (not reserved, and
standard C already owns `sys_errlist`, `sys_nerr` and `system()`);
bare `__` (the status quo).

### 3.1 ccpp: the CMDSYS ids leave the name pool (fewer bytes, not more)

**The problem.** Done naively, the longer spellings cost ccpp, the
program with the least room after cccomp (763 bytes to spare
running, doc/measure.txt). The 25 CMDSYS spellings sit in ccpp's
`fixed[]` table (src/ppname.c:64), and `pp_names_init()`
(src/ppname.c:133) interns them into the name pool, where
`MAX_PP_POOL` (8,432) peaks at 8,130. Four more characters each
would add about 100 bytes to the table and 100 to the pool: 200 of
ccpp's 763. (cccomp and ccgen don't pay: src/nam.c streams the name
file and looks spellings up by id, keeping none.)

**The decision: make ids 1 to `NCMDSYS` virtual in ccpp.** They're
fixed, so ccpp needn't keep them in the pool at all:

- The table becomes one packed string of the 25 PLASMA names,
  lowercase, each after a length byte, with no prefix and no
  pointers: about 150 characters and 25 length bytes, 175 bytes
  against today's 275 (200 characters, 25 NULs and 25 pointers).
- `pp_names_init()` interns only the fixed names from `main` on, and
  starts `nids` at `NCMDSYS`. Pool ids are numbered from
  `NCMDSYS + 1`: `find()` (src/ppname.c:94) adds `NCMDSYS` to its
  count, and `entry_at()` (src/ppname.c:112) takes `id - NCMDSYS`.
- `pp_intern()` (src/ppname.c:157) first checks for the prefix (a
  test of the first character, `_`, rejects nearly every identifier at
  once). For `__cmd_x`, it searches the packed table for `x` and
  returns its id, bypassing the recent-names cache. A `__cmd_x` that
  isn't one of the 25 falls through and is interned as an ordinary
  name, with its whole spelling, so that the linker can name it in
  its diagnostic (below).
- `pp_id_spelling()` (src/ppname.c:214) builds `__cmd_` and the
  table's name for an id up to `NCMDSYS`, for messages.
  `pp_name_hash()` (src/ppname.c:227), used for struct tags, hashes
  the table's name for them: any value that depends only on the id
  works, since these spellings are fixed. (A struct tagged `__cmd_puts`
  is legal but absurd; it must still work.)
- `pp_names_write()` (src/ppname.c:272) writes the 25 first, with the
  prefix, so the name file's format and every reader are unchanged.

**Cost.** The pool holds 225 bytes less (the 25 entries and their
length bytes), so `MAX_PP_POOL` drops by 225 to keep today's
headroom; the table is 100 bytes smaller; the code grows by an
estimated 60 to 100 bytes. Net, about 250 bytes *less* in ccpp than
today, whatever the prefix. It's also faster: every pool walk (the
cache's miss path, about 5,500 ops, approach.md "ccpp's name
lookup") skips 25 fewer entries. Measure it with `make measure`, and
make it its own commit, with the current `__` spelling (section 9,
step 6), so the saving is measured apart from the rename.

### 3.1a Spare CMDSYS ids, so a new one renumbers nothing

**Decision (2026-11-13).** Ids 1 to `CMDSYS_IDS`, 40, are
the CMDSYS block: the 25 in use (`NCMDSYS`) and 15 spare. `main`,
`pstr` and the other fixed ids follow the block (`main` 41 to
`__plasma_import` 48), and each file's own names start at 49. When a
later PLASMA adds a CMDSYS function, ccish appends it to the packed
table and to `cmdsys_sigs[]` (src/parse.c:209) and raises `NCMDSYS`;
no other id moves, so no token stream, name file or test expectation
changes for the names already there.

**Why it's nearly free.**

- Ids are words in every stream (CCPP.md's token table), so a higher
  number costs nothing to write.
- No table in any pass is indexed by id: cccomp's `MAX_IDS`
  (src/name.tab.h:9) counts enum constants and typedef names, not ids,
  and src/nam.c looks spellings up by reading lines.
- With section 3.1, the block is virtual in ccpp: the spare ids are in
  neither the packed table nor the pool. `pp_names_init()` starts
  `nids` at `CMDSYS_IDS`, `find()` and `entry_at()` offset by
  `CMDSYS_IDS` rather than `NCMDSYS`, and `pp_intern()` only ever
  returns ids up to `NCMDSYS` from the table.
- The name file needs a line for each spare id, since line *n* is id
  *n* and a blank line ends the names (src/nam.c:105-108).
  `pp_names_write()` writes `?` for each: not an identifier, so it
  can't be mistaken for one, and `nam_open()` accepts it (it checks
  only the length and a leading `@`). That's 30 bytes more in each
  name file on disk, which the readers stream; and a few bytes of code
  in ccpp.

**How many spare.** PLASMA 2.20's CMDSYS exports 26 functions
(src/inc/cmdsys.plh:69-74): the 25, and `divmod`, which ccish leaves
out on purpose (its two results come in opposite orders on plvm and
the 6502 VMs). It has grown slowly, so 15 spare (the user's choice,
2026-11-13) is a generous margin, at 30 bytes a name file.

**The one-time renumbering.** Moving `main` and the rest changes
every `.i` and `.nam` once, so it goes with the spelling change
(section 9, step 7), which regenerates those expectations anyway.
Bump `NAM_VERSION` with it, as src/fixed.h:3 asks, so a name file
from before is rejected rather than misread (a token stream is tied to
its name file by the stamp). Update src/fixed.h
(`ID_MAIN` and the rest, and the new `CMDSYS_IDS`) and CCPP.md's
fixed-id list (CCPP.md:269-278, 297-309).

### 3.2 The rest of the rename

- The linker's own references: the `use_cmdsys()` calls
  (`"__heapavail"`, `"__isult"`, `"__heapalloc"`, `"__memset"`,
  `"__puts"`, src/link.c:677-684), the helper names
  `HELP_CMDSYS_NAMES` (src/obj.fmt.h:34, used at src/link.c:167 and
  450), and init's `gsym_find("__isult")` (src/link.c:1520). The
  import-stripping test in `write_imports()` (src/link.c:1618, today
  `name[0] == '_' && name[1] == '_'`) strips `__cmd_` instead. The
  ESD and the image's import names are PLASMA names (uppercase), so
  neither the REL module nor the image format changes. The linker's
  shared-name pool grows by 4 bytes for each CMDSYS name a link uses
  (about 60 bytes when linking cccomp; `MAX_GNAMES` is 3,000, with a
  peak of 2,543).
- The comments at src/ppname.c:55-63 and src/parse.c:199-208, and
  CCPP.md's fixed-id list (CCPP.md:297-309). `cmdsys_sigs[]`
  (src/parse.c:209-218) and `find_cmdsys()` (src/parse.c:220-226) are
  untouched (they work on ids, not names).
- **The churn is large but mechanical:** 138 files outside
  `tests/out` use `__put*` alone. The name files (`.nam`) hold the
  spellings, so the pp tests' `.nam` expectations change (e.g.
  tests/pp/directives.nam:3, `__puts`); the `.i` token streams don't
  (they hold ids). Call sites: the C library's wrappers (lib/memcpy.c,
  lib/memset.c, lib/strncpy.c, lib/getc.c; grep for the full set),
  inc/ptest.h:49-66 (the cross-test mapping), samples/rod.c and
  samples/rodfast.c, tests/errors/proto_mismatch.c, every test, and
  the user-facing docs (README.md's hello example, doc/ccish-guide.md).
  It's a user-visible change: programs written for ccish today must
  change their spellings.
- **Two new checks, both in the linker** (cclink has about 13K to
  spare; ccpp and cccomp have none to give):
  - Defining a name that begins `__cmd_` is an error (one message in
    ccish.msg). It's C-correct, since user code may never define
    double-underscore names (C11 7.1.3). **It changes today's rule**:
    `use_cmdsys()` says a CMDSYS name is an import "unless a program
    defines its own" (src/link.c:657). No test defines one now (I
    grepped); check again when implementing.
  - An *undefined* `__cmd_` name that isn't one of the 25 gets "not a
    CMDSYS function" instead of a bare 172, naming it: it catches a
    typo like `__cmd_strcp` at the right moment. Recommended
    (section 7).

## 4. The C library's private names drop to one underscore

**Decision.** `__start` to `_start`, `__path` to `_path`,
`__filetype` to `_filetype`, `__exitcap` to `_exitcap`.

**Why.** It makes the rule three tiers, readable at a glance:
`__cmd_x` is the VM's, `_x` is ccish's implementation's (C11 7.1.3
reserves file-scope names beginning with a single underscore for the
implementation: the standard's own home for implementation names),
and `x` is C's and the user's. The user is happy to make local
renames for clarity or space (2026-11-12).

**Record: `__filetype` is a C library function, not a CMD one.** It's
defined in lib/fopen.c; it sets the default ProDOS type and aux type
that `fopen` creates files with. Neither CMDSYS (src/inc/cmdsys.plh)
nor FILEIO (src/inc/fileio.plh) exports anything called filetype.

**What it touches.** lib/start.c (`int __start(void)`), lib/path.c,
lib/fopen.c, lib/remove.c (its `__path` declaration and call),
lib/exit.c (the `__exitcap` array and the comments naming it),
lib/include/stdio.h (`__filetype`'s declaration: the one
user-visible API change), src/link.c (`exit_cap()`'s
`gsym_find("__exitcap")`, and the `__start` lookups in `resolve()`,
src/link.c:712-716), tests/special/lib.sh, doc/ccish-guide.md,
doc/bootstrap-test.md.

## 5. `-L dir`: the linker adds the objects it needs

**Decision.** `cclink [-v] [-x] [-L dir] [-o module] output|@file...`
gains `-L`, with its directory a **required** parameter. With `-L`,
the linker adds, from `dir`, the library objects that define the
names the program needs, until none remain, and **`dir` must have a
`lib.map`** (section 5.2) that says which object defines each name.
Without `-L`, behavior is exactly today's: an undefined plain name is
error 172. **No implicit search**: an implicit default directory
would make link behavior depend on what happens to be on the disk,
and it's platform-dependent; the two error cases in
tests/special/lib.sh (nostart, noexit) assert the no-search behavior.
The `ccish` driver (src/main.c) takes the same `-L` and passes it to
`link_module()`: a few lines, since the host driver links in-process.

`-L` is orthogonal to `-x`: the objects are added in step 1 of
link.c, and the write path (REL or image) is untouched.

### 5.1 What the linker looks up

A used, undefined name reaches 172 only through `resolve()`
(src/link.c:669-720). A CMDSYS fixed id becomes an import there
(`G_IMPORT`). A name declared as a PLASMA import (an asm label, or
`#pragma plasma import`; decision 35, src/ptop.c:579-606) is an
import from its module. A bad CMDSYS declaration is 173
(src/link.c:698-700). Only a plain name (`G_DATA` or `G_PLAIN`, with
no label; the G_ flags at src/link.c:177-178) is 172
(src/link.c:701). So the **wanted names** are:

1. Each plain name that's used, not defined, and has no asm label:
   exactly the names that would be 172. CMDSYS names and PLASMA
   imports are never looked up.
2. **`_start`, when main has two parameters.** No object references
   it: `resolve()` asks for it only because of main's parameter count
   (src/link.c:712-716), and today tools/cclink adds start only by
   reacting to that 172. The lookup must ask for it the same way, or
   `main(argc, argv)` programs won't link with `-L`. (Test it: lib.sh's
   `args` program.)

An added object's own needs are found the same way, since its
summary adds its names to the table (section 5.3). `resolve()` itself
stays as it is: after the lookup, whatever is still undefined is 172,
or 172 for `_start`, as today.

### 5.2 The map file: `lib.map`, generated

**Format:** one line per object, the object first, then the names it
defines:

```
fopen fopen fclose _filetype
start _start
exit exit _exitcap
```

- The first token is the object's **base name, no extension**, as the
  linker takes it everywhere. The rest are the **exact C names**, as
  they sit in the name table. Inverted from `NAME BASE` because it
  repeats no file names (an object that defines several names is one
  line), it reads as "this object defines these", and a generator
  emits it per object.
- Line endings: **CR or LF ends a line, blank lines skipped**, as
  `read_list()` does (src/ccrel.c:68-89): it quietly accepts Unix LF,
  ProDOS CR, and Windows CRLF. One space between tokens. No comments.
- **It's generated, not hand-written:** a host tool,
  `tools/libmap.py`, reads each `.syms` file in a directory
  (OBJECT.md's format) and writes the line for each object, listing
  its defined non-local names. It **fails if two objects define the
  same name**, so a map can't be ambiguous. The Makefile rule that
  builds the library objects runs it, and tools/a2disk writes the
  result to the CCLIB disk as `LIB/LIB.MAP`. A map that's always
  complete needs no fallback, so there are **no other tiers**: no
  probe of `dir/NAME.syms`, no underscore stripping, no built-in alias
  table (`fclose` is simply on fopen's line). Someone who adds their
  own objects to a library directory on the Apple edits LIB.MAP by
  hand (or regenerates it on a host).
- **One library directory, for plvm and the Apple alike.** One link
  sequence must give a module that runs on both (the user,
  2026-11-13), so there's one `exit`, lib/exit.c's, working on both
  (section 5.5), and one directory, `$(OUT)/lib`, with one generated
  map. The tests, the bootstrap, xload_self.sh, measure.py, `make
  apple` and the CCLIB disk all link with it.
- The linker checks that an object the map names really defines the
  name: the map is generated, but a library directory can be edited
  by hand on the Apple. The check is free (section 5.3).

### 5.3 The algorithm: one file open at a time

```
before anything else: open dir/lib.map and close it; if that fails,
  stop with "cannot open" (171) and the map's path, before any
  object is read.
step 1, as today: read each named object's summary.
repeat:
  open the map; read it a token at a time; for each line, if any
    name on it is wanted (section 5.1):
      if its base is already linked: the map is wrong (that object
        doesn't define the name), an error naming both;
      else note the base (add it to the object list).
  close the map.
  read the summary of each base noted in this pass, in order.
until a pass notes nothing.
then resolve(), as today.
```

- **One file open at a time.** The map is closed before the summaries
  it named are read, so the link never has two files open, and on an
  Apple II never needs a second ProDOS I/O buffer (1K of heap each,
  lib/fopen.c). That's why each pass collects first and reads after.
- **Existence is checked early, where it pays.** The map's own
  existence is checked before any work, since `-L` can't do anything
  without it. An object's existence needs no separate check: its
  summary is the first file of it the link reads, and failing to open
  it is the error (a map that names a missing object is a broken
  library directory; there's nothing to fall back to). A separate
  probe would be a second open of the same file, and on ProDOS no
  cheaper: plvm can't ask about a file without opening it
  (`sysgetfileinfo` is unimplemented, src/vmsrc/sysio.c:251-253).
- **The drift check is free:** a name the map assigned to an object
  that didn't define it is still wanted in the next pass, and its base
  is already linked. A pass that notes nothing ends the loop, so the
  check needs no memory.
- **Passes:** one per level of library dependency (about 3 for ccish's
  passes: e.g. fopen needs path, which needs strlen). The map is about
  350 bytes, read 3 or 4 times.
- **Where the names live.** Added objects need their names stored:
  `bases[]` (src/ccrel.c) holds pointers, into argv or into the
  512-byte `list[]` buffer that `@file` lists fill (src/ccrel.c:36).
  Store each added base, without the directory, in `list[]` (about 20
  objects at about 8 bytes, 180 bytes), and record the index of the
  first added object: objects from there on are opened as `dir/base`,
  built in one `MAX_PATH` buffer when opened (the `stream_name()`
  pattern, src/diag.c). Raise `LIST_BYTES` if measure says so, and
  report it.
- **Limits:** linking ccpp and cccomp each takes 28 of `MAX_OBJS` 32
  (src/link.h:5) today, the library's objects included. `-L` doesn't
  change that, but it's close; error 223 ("too many objects to link")
  already exists.
- **Determinism** (the bootstrap must stay byte-for-byte): passes in
  order, the map in file order within a pass. The objects may come in
  a different order than tools/cclink's, which changes the modules'
  bytes once; the bootstrap compares stages with each other, so that's
  fine.
- **The listing names the objects added.** With `-v`, the linker
  lists the objects it added, so measure.py can write
  `tools/out/PROGRAM.objs` (which tools/a2disk and xload_self.sh
  read) without tools/cclink.
- **Reuse the existing machinery.** Each added object goes through
  `read_summary()` (src/link.c:627): it does the layout accounting,
  the zero placement, and `add_def` for `-x`. Objects are appended
  after the named ones, as tools/cclink appends them today.

### 5.4 Cost and tests

**Cost.** Smaller than the first estimate, with no probes, tiers or
alias table: perhaps 0.5K of code in the linker (the option, the map
reader, the loop, 2 or 3 new ccish.msg entries), against its about
13K spare running. Measure it with `make measure` and report it, as
usual; the linker isn't the constrained program.

**Tests** (tests/special/cclink.sh, the renamed script, plus a run
test for the driver's `-L`): a program that needs objects two levels
deep (fopen, then path, then strlen); `main(argc, argv)`, which needs
`_start`; no map (171, before any
object is read); a map naming a missing object (171 for its summary);
a map naming an object that doesn't define the name (the drift
error); a name on no line (the 172 text); libmap.py rejecting a name
two objects define; and linking twice gives a byte-identical module.
The lib.sh nostart and noexit cases keep passing unchanged (no `-L`,
no search).

### 5.5 One exit for plvm and the Apple

**Decision (2026-11-13).** lib/exit.c's `exit` works under plvm as
well as on both Apple VMs, so that one library serves both, and
tools/plvmexit.c, tools/cclink's `-a`, xload_self.sh's substitution
of plvmexit, and tools/a2disk's leaving it off the CCLIB disk all go.
It lands first (section 9, step 1), since it leaves `-L` one `exit`
to find.

**Two things stop lib/exit.c under plvm today.**

1. **Init's save routine finds its area through TMP** ($E7, a
   zero-page pointer), which the Apple VMs' ICAL sets to the routine's
   address and plvm's doesn't (src/vmsrc/plvm.c:418-427): there, it
   would write through whatever $E7 holds. **Fix:** init sets the
   pointer itself, `LA __exitcap; DUP; SAW $00E7; ICAL` (`_exitcap`
   after step 7; src/link.c's
   `init_code()`; 4 bytes more, `init_size()` with it). On the Apple,
   ICAL sets the same value again; under plvm nothing touches $E7
   before the call. The routine then runs on plvm's emulated 6502,
   harmlessly: it writes only its own area, and puts X back. (A
   native test of page 3 can't tell the two apart instead: plvm's heap
   starts at $0300, src/vmsrc/cmdsys.c:35.)
2. **exit's restore routine can't unwind plvm**, whose callers' IPs
   are in C frames (doc/plasma-requests.md, item 6). **Fix:** exit
   checks its own def entry first, as its first version did: byte 2
   is $03 (the VM's entry, `JSR $03xx`) on an Apple and $FF under plvm
   (`JSR $FF08`). Under plvm it prints ccstat's marker with the status
   and runs a BRK, which stops plvm (plvm reports only the BRK's
   address, src/vmsrc/plvm.c:39-62, so the status must go out as text
   first).

**A shorter marker.** Most of the plvm path's bytes would be the
marker, today `^A exit N` and a newline (tools/ccstat.pla:54). It
becomes `^A` followed by the number: exit's plvm path is then
`__putc(1); __puti(status);` and the BRK, with no string, no
newline and no `& 255`. tools/ccstat.pla prints the same after a
normal return (it can keep its newline). tools/ccrun's awk changes to
match:

- An exit marker is `^A` followed by a number (it may be negative:
  `exit(-1)`), which ccrun reduces mod 256, as `& 255` did. Its own
  marker, `^A plvm N`, starts with a letter, so the two can't be
  confused.
- Since exit's marker has no newline, ccrun's own marker follows it on
  the same line: the awk splits each line at every `^A` (today it
  handles one), keeping text before the first, as now.

**Cost.** About 28 bytes per pass, against today's Apple-only
lib/exit.c: 4 in init, and about 24 in exit (the def-entry check 10,
the two calls 9, the BRK's ICAL 4, and a zero byte in the zero region
for the BRK). Estimated running spare: ccpp about 735, cccomp about
719 (today 763 and 747). Measure it. (Testing TMP in the restore
routine instead of the def entry would save 2 to 4 bytes more; the
def-entry check is simpler, and runs in bytecode where it can be
read.)

**Tests.** lib.sh's quit test links lib/exit.c's `exit` and runs it
under plvm (exit from five calls deep gives status 3, after the
output), so every plvm run that links exit now also runs the save
routine on plvm's emulated 6502; add `exit(-1)` (status 255) and
`exit(0)` from a nested call; keep lib.sh's check of init's code (now
with the `DUP; SAW $00E7`). ccrun's parsing: output that doesn't end
its line before the marker. On MAME's 64K and 128K IIe, again: a
pass's error gives `ERR:$01`, and a program compiled there that calls
`exit(3)` deep gives `ERR:$03`, twice, with the next commands working.

## 6. Other modules: decision 35 stands, recorded

No name-mangling outside CMDSYS (approach.md:4995-5039). Table
modules (FILEIO's shape: one exported table,
src/libsrc/apple/fileio.pla:33-36; the interface
src/inc/fileio.plh:47-66) are structs of function pointers after
`#pragma plasma import` (lib/include/fileio.h), and **the module
name is the qualifier**: `fileio.open` can't collide with any
global `open`, so table modules need no prefix at all. Convention to
add to the guide (section 11.1): the table's global takes the
module's name in lower case (`fileio`, `conio`). Named-symbol
modules: a function or extern declared with an asm label
(`__asm__("PERR")`), or after a pragma in the same file, is a PLASMA
import *if no linked object defines it*; the PLASMA name is the
label, else the uppercased C name (src/ptop.c:579-606; limits
MAX_IMPORTS 16 and MAX_DEPS 4; the linker's MAX_MODS 8 at
src/link.c:93). The `__cmd_` re-spelling updates decision 35's
CMDSYS note; its other-modules design is unchanged. **Later:** a
`.plh`-driven header generator, `fileio.h` first. Its own comment
concedes `geteof` and `getmark` are untested `void *` fields
(lib/include/fileio.h:1-8): the hand-maintained mirror of
`struc t_fileio` drifting. The Makefile already depends on
`../src/inc` (the ccstat rule, `cmdsys.plh`), so the build precedent
exists.

## 7. Pending choices (the user's)

1. The new letters for the driver's and ccgen's `-d` and `-i`:
   proposed `-m` (machine ops) and `-r` (IR).
2. The "not a CMDSYS function" diagnostic for an unknown `__cmd_`
   name: recommended yes (a few lines, one message).
3. Later, optional: the `.plh`-driven headers.

Decided in review (2026-11-13): rename the driver's case pairs too
(section 2); `_start` is wanted for main with parameters (5.1); the
map is generated and required, with no fallback tiers and no alias
table (5.2); one library directory and one `exit` for plvm and the
Apple, with init setting TMP and a shorter ccstat marker (5.2, 5.5);
tools/cclink
is deleted rather than renamed (section 9); ccpp's CMDSYS ids leave
its name pool (3.1), with 15 spare CMDSYS ids after the 25 (3.1a).

## 8. Verified facts this record rests on (2026-11-13)

- **Options:** `is_opt()` matches a letter in either case
  (src/diag.c:32-36); the passes use it because CMD uppercases the
  command line. The driver compares exactly (`strcmp`, src/main.c),
  and has `-i`/`-I` and `-d`/`-D`. ccpp's `-I`, `-D` and `-U` go
  through `pp_arg()` (src/pp.c).
- **The fixed ids:** `fixed[]` at src/ppname.c:64-71 (the 25 CMDSYS
  spellings, then `main`, `pstr`, `__CCISH__`, `__FILE__`, `__LINE__`,
  `defined`, `__asm__`, `__plasma_import`); `NFIXED` 33 and `NCMDSYS`
  25 at src/fixed.h:7-10; `pp_names_init()` interns them all
  (src/ppname.c:133-140); CCPP.md:297-309 lists them. `MAX_PP_POOL`
  is 8,432, with a peak of 8,130 (doc/limits.md). The pool's
  readers: `find()`, `entry_at()`, `pp_intern()`, `pp_id_spelling()`,
  `pp_name_hash()` and `pp_names_write()`, all in src/ppname.c.
- **The name file's readers keep nothing:** src/nam.c (cccomp and
  ccgen) streams NAME.nam and looks spellings up by id.
- **CMDSYS names in the linker:** `use_cmdsys()` (src/link.c:658-664,
  "an import, unless a program defines its own" at :657); its calls
  at :677-684 and :450; `HELP_CMDSYS_NAMES` at src/obj.fmt.h:34; the
  import-stripping test at src/link.c:1618.
- **The C library's `__` private names:** lib/start.c, lib/path.c,
  lib/fopen.c (`__filetype`), lib/exit.c (`__exitcap`, which init runs
  just before main, `exit_cap()` in src/link.c). No `filetype` in
  CMDSYS or FILEIO.
- **Two objects define `exit` today:** lib/exit.c (the Apple's) and
  tools/plvmexit.c (plvm's). tools/cclink picks plvmexit's unless
  `-a`, and both objects are in `$(OUT)/lib`; xload_self.sh
  substitutes plvmexit's into measure.py's lists, and tools/a2disk
  leaves plvmexit off the CCLIB disk. Section 5.5 removes all that.
- **plvm and native code:** plvm's ICAL runs the address as 6502 code
  on its emulator without setting TMP (src/vmsrc/plvm.c:418-427); a
  BRK makes it print the BRK's address and exit
  (src/vmsrc/plvm.c:39-62); its heap starts at $0300
  (src/vmsrc/cmdsys.c:35); a def entry is `JSR $FF08` there and
  `JSR $03xx` on the Apple VMs. ccstat's marker: tools/ccstat.pla:54;
  ccrun's parser: tools/ccrun's awk.
- **The link's routing of names:** `read_summary()` src/link.c:627;
  `resolve()` src/link.c:669-720 (173 at :698-700, 172 at :701,
  `__start` at :712-716); `undefined()` :334; the G_ flags :177-178.
  Step 1 reads every summary, then `resolve()` runs (src/link.c:1757-
  1759).
- **The linker's object names:** `bases[MAX_OBJS]`, `LIST_BYTES` 512
  and `list[]` (src/ccrel.c:31-38), `add_base()` :60, `read_list()`
  :68; `MAX_OBJS` 32 (src/link.h:5); objects per program today: ccpp
  28, cccomp 28, ccgen 27, ccrel 23 (tools/out/*.objs).
- **Files on the VMs:** FILEIO's open takes a free 1K I/O buffer
  (src/libsrc/apple/fileio.pla:241; lib/fopen.c's accounting);
  `sysgetfileinfo` is unimplemented on plvm
  (src/vmsrc/sysio.c:251-253).
- **The fits:** ccpp 763 and cccomp 747 bytes to spare running; the
  linker about 13K (doc/measure.txt).

## 9. Commit plan

Each commit: `make test` and `make bootstrap`; `make measure` after
any size-affecting one, committing doc/measure.txt (and
doc/limits.md) with it. Stage files by explicit path (the
leading-comma temp-file convention, AGENTS.md).

1. **One exit** (section 5.5): init sets TMP; lib/exit.c's plvm path;
   the shorter marker in tools/ccstat.pla and tools/ccrun; delete
   tools/plvmexit.c and its Makefile rules (and `BOOT_LIB`'s mention),
   tools/cclink's `-a` (and measure.py's use of it), xload_self.sh's
   substitution (it goes back to copying measure.py's modules) and
   tools/a2disk's exclusion; the tests; MAME on both IIe models.
2. **Options** (section 2): the linker's and the driver's `-l` to
   `-v`; the driver's and ccgen's `-d` and `-i` to the letters
   chosen. The usage lines, the Makefile, tools/listings.py,
   tools/measure.py, the scripts and the guide. This must come before
   `-L`.
3. **`-L`** (section 5): tools/libmap.py and the library's map in the
   Makefile; `-L` in the linker and the driver,
   with `_start`, the early map check, the drift check, and the
   listing of the objects added; the section 5.4 tests.
4. **Use `-L`, and delete tools/cclink:** measure.py and bootstrap.sh
   link with `-L $(OUT)/lib`, and measure.py writes `PROGRAM.objs`
   from the listing; tools/a2disk writes `LIB/LIB.MAP` and its usage
   example becomes `CCREL -L LIB -O PROG PROG` (CCLINK after step 5);
   lib.sh's quit test links with `-L` too. Then delete tools/cclink
   and its Makefile dependencies.
5. **Rename ccrel to cclink** (section 1's list).
6. **ccpp's CMDSYS ids leave the pool** (section 3.1), with today's
   `__` spelling: a pure saving, measured on its own, with
   `MAX_PP_POOL` lowered.
7. **The `__cmd_` spellings, the spare CMDSYS ids, the `_` private
   names, and the two checks** (sections 3.1a, 3.2 and 4), with
   `NAM_VERSION` raised and the regenerated pp `.nam`
   expectations and inc/ptest.h. With step 6 done, the prefix itself
   is one string in ccpp.
