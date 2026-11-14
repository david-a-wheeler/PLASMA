# Linking decisions: cclink, -L, lib.map, and the __cmd_ names (done)

**This work is finished (2026-11-14), and this file is now its
record:** read it for why ccish links as it does, not for what to do
next. For what ccish does now, the code and doc/ccish-guide.md are
the reference.

The record of one collection of decisions, made in discussion
2026-11-12 and revised after review 2026-11-13, about the final pass
and how names mean things at the link: **approach.md's decision 46**
points here. Each section gives the decision, the rationale, the
options that lost, and what implementing it touched. The file and
line references, and words like "today", are as of when each part was
written, before the change: since then src/ccrel.c is src/cclink.c,
the CMDSYS names are `__cmd_puts` and so on, and the line numbers
have moved. Section 8 is the list of verified facts the decisions
rested on; section 9, the steps as done, with each one's measurements
and what the plan got wrong.

**Status (2026-11-14): done.** All seven steps of section 9 are
done: the one exit (section 5.5), the option letters (section 2, with `-m` and
`-r` as proposed), the library search, `-L dir` (section 5), using
`-L` everywhere, with tools/cclink deleted (section 9, step 4), the
rename of ccrel to cclink (section 1), ccpp's CMDSYS ids out of its
name pool (section 3.1), and the `__cmd_` spellings with the spare
ids, the `_` private names and the two link checks (sections 3, 3.1a,
3.2 and 4, with the diagnostic of section 7, item 2: yes). Section 9,
step 7, says what the plan got wrong.

## 1. The final pass is renamed `ccrel` to `cclink` (done, 2026-11-14)

**Decision.** The program, its source file, the Makefile targets,
the tests and the docs: `ccrel` becomes `cclink`.

**Done** (section 9, step 5, 2026-11-14), as described here: a
substitution of `ccrel` and `CCREL` in every tracked file but
approach.md (history), then this file's record by hand. The C
library's and the passes' source comments, the tests, the tools and
the docs went too, beyond the list below.

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

**What it touches.** The Makefile (`PROGS`, `SRC_ccrel` and its
prerequisite line, from which every other list of the programs
comes, and the mentions of `ccrel-small` in the xload, xload_self and
apple rules: `grep -n ccrel Makefile` finds them all); `src/ccrel.c` renamed to
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

## 2. Options: no two in one program share a letter (done, 2026-11-13)

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

**What it touches.** `src/cclink.c`'s usage and option loop
(src/cclink.c:49-57, 99); `src/main.c`'s usage and parse
(src/main.c:72-110); `src/ccgen.c`'s; the Makefile's rules that pass
`-l`, `-d` or `-i` (the run rule's `./ccish -l -o ...`, the gen
rule); tools/listings.py (`ccgen -d`); tools/measure.py (it passes
`-l` to the linker); the special scripts that pass `-l`
(tests/special/cclink.sh); the guide's option text.

## 3. CMDSYS functions are spelled `__cmd_` + the PLASMA name (done, 2026-11-14)

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

### 3.1 ccpp: the CMDSYS ids leave the name pool (fewer bytes, not more; done, 2026-11-14)

**The problem.** Done naively, the longer spellings cost ccpp, the
program with the least room after cccomp (740 bytes to spare
running, doc/measure.txt). The 25 CMDSYS spellings sit in ccpp's
`fixed[]` table (src/ppname.c:64), and `pp_names_init()`
(src/ppname.c:133) interns them into the name pool, where
`MAX_PP_POOL` (8,432) peaks at 8,130. Four more characters each
would add about 100 bytes to the table and 100 to the pool: 200 of
ccpp's 740. (cccomp and ccgen don't pay: src/nam.c streams the name
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

**Measured** (step 6, 2026-11-14): ccpp's running spare went from 740
to 868 bytes, 128 more, half the estimate; loading, 130 more. The pool
holds 225 bytes less (its peak 8,130 to 7,905, and `MAX_PP_POOL` 8,432
to 8,207), and the table and the strings it replaced are 124 bytes
smaller; but ppname.c's code grew 209 bytes (1,003 to 1,212), not 60
to 100. Where: the `__` test and the table's walk in `cmdsys_find()`
(94 bytes) and `cmdsys_at()` (33); `pp_id_spelling()`'s "__" and the
table (42 more); `pp_names_write()`'s 25 CMDSYS lines (53 more); and
`find()`'s ids from `NCMDSYS + 1` (14 more). `pp_intern()` got 10
bytes smaller, with one exit and one cache update. Walking the table
with a pointer, not an index, saved about 20 bytes, since every
`cmdsys[i]` was an `LA` and an `ADDLW`. ccpp runs 3.8% fewer ops
compiling ccish's own sources (`make profile`: 572.8M against 595.6M),
since every pool walk skips 25 entries. The token streams and name
files are unchanged: every pp test's `.i` and `.nam` are the same.

### 3.1a Spare CMDSYS ids, so a new one renumbers nothing (done, 2026-11-14)

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

### 3.2 The rest of the rename (done, 2026-11-14)

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
  (they hold ids). (Wrong, as step 7 found: 3.1a's spare ids move
  every file's own ids by 15, so every `.tok` changed too.) Call sites: the C library's wrappers (lib/memcpy.c,
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
    grepped); check again when implementing. (Wrong, as step 7 found:
    two did; section 9, step 7.)
  - An *undefined* `__cmd_` name that isn't one of the 25 gets "not a
    CMDSYS function" instead of a bare 172, naming it: it catches a
    typo like `__cmd_strcp` at the right moment. Recommended
    (section 7).

## 4. The C library's private names drop to one underscore (done, 2026-11-14)

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

## 5. `-L dir`: the linker adds the objects it needs (done, 2026-11-13)

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
   (src/link.c:712-716), and a library search that reacts to that 172
   (as tools/cclink's did, until step 4 deleted it) adds start only
   that way. The lookup must ask for it the same way, or
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
  `read_list()` does (src/cclink.c:68-89): it quietly accepts Unix LF,
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
  `bases[]` (src/cclink.c) holds pointers, into argv or into the
  512-byte `list[]` buffer that `@file` lists fill (src/cclink.c:36).
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
  a different order than tools/cclink's (deleted in step 4, whose
  reaction order they replaced), which changes the modules' bytes
  once; the bootstrap compares stages with each other, so that's fine.
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

### 5.5 One exit for plvm and the Apple (done, 2026-11-13)

**Decision (2026-11-13).** lib/exit.c's `exit` works under plvm as
well as on both Apple VMs, so that one link gives a module that runs
on both, and one library serves both. tools/plvmexit.c, tools/cclink's
`-a`, xload_self.sh's substitution of plvmexit, and tools/a2disk's
leaving it off the CCLIB disk are gone. It's a stopgap, about 23
bytes in every program that links exit, until plvm can unwind or
exit with a status (doc/plasma-requests.md, items 2 and 6); then the
plvm path and init's setting TMP can go.

**What was done** (section 9, step 1):

1. **Init sets TMP itself.** The save routine finds its area through
   TMP ($E7, a zero-page pointer), which the Apple VMs' ICAL sets to
   the routine's address and plvm's doesn't
   (src/vmsrc/plvm.c:418-427). So init runs `LA __exitcap; DUP;
   CB $E7; SW; ICAL` (src/link.c's `init_code()`, `ZP_TMP`; 4 bytes
   more). Not `SAW $00E7`, the same size, as first done: the Apple
   VMs' SAW stores through TMP itself (plvm02.s, plvmjit02.s), so its
   first byte moves TMP and its second lands at $00xx+1, xx being the
   routine's address's low byte. Step 3's code moved `__exitcap` in
   cclink's image to $27F7, which put $27 in $F8, the 64K VM's dispatch
   page (OPPAGE), and the image trapped at $0000 on the next opcode;
   before, the byte had landed somewhere harmless, by luck. SW stores
   through the evaluation stack, on every VM. The routine then runs
   on plvm's emulated 6502, harmlessly: it writes only its own area,
   and puts X back.
2. **exit tells plvm apart by $F1** (`ON_APPLE` in lib/exit.c:
   `LAB $00F1; BRTRU`, 6 bytes, against 10 for a test of exit's def
   entry). On both Apple VMs, $F1 holds the opcode of the zero-page
   fetch loop's `LDA IP,Y` ($B9; plvm02.s and plvmjit02.s copy the loop
   to $EF); plvm's zero page is 0 there, since its IP is a C variable
   and it uses only $A0-$E5. (A test of page 3 can't work: plvm's heap
   starts at $0300, src/vmsrc/cmdsys.c:35.)
3. **Under plvm, exit prints a shorter marker and runs a BRK.** The
   marker is `^A` and the number (`__putc(1); __puti(status)`), with
   no string, no newline and no `& 255`. The BRK is a 0 byte already in
   the save routine (`BRK_AT`, the high byte of its `LDA $009F,Y`), so
   it needs no byte of its own. plvm reports only the BRK's address
   (src/vmsrc/plvm.c:39-62), which is why the status goes out as text
   first. tools/ccstat.pla prints the same marker after a normal return
   (with its newline). tools/ccrun's awk splits each line at every
   `^A`: a marker that's a number (it may be negative) is the status,
   mod 256; `plvm N` is plvm's own; text before the first `^A` is
   output.

**Cost, measured:** 23 bytes in each pass (19 in exit, 4 in init).
Running spare: ccpp 740 and cccomp 724 (they were 763 and 747).

**Tests:** lib.sh runs `exit` from five calls deep under plvm with
statuses 3, -1 (255) and 0, one of them after output that doesn't end
its line, and checks plvm's BRK message; and checks init's code,
`LA data+0|DUP|CB 231|SW|ICAL|...`. On MAME's 64K and 128K IIe: a
pass's error gives `ERR:$01`, and a program compiled there by the
passes that calls `exit(3)` deep gives `ERR:$03`, twice, with the
next commands working. `make test` and `make bootstrap` pass.

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

## 7. The user's choices (made)

1. The new letters for the driver's and ccgen's `-d` and `-i`:
   `-m` (machine ops) and `-r` (IR), as proposed (2026-11-13, step 2).
2. The "not a CMDSYS function" diagnostic for an unknown `__cmd_`
   name: yes (2026-11-14, step 7; cclink's message 300).
3. Later, optional, and not done: the `.plh`-driven headers.

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
- **One object defines `exit`:** lib/exit.c, for plvm and the Apple
  (section 5.5).
- **plvm and native code:** plvm's ICAL runs the address as 6502 code
  on its emulator without setting TMP (src/vmsrc/plvm.c:418-427); a
  BRK makes it print the BRK's address and exit
  (src/vmsrc/plvm.c:39-62); its heap starts at $0300
  (src/vmsrc/cmdsys.c:35); a def entry is `JSR $FF08` there and
  `JSR $03xx` on the Apple VMs. ccstat's marker: tools/ccstat.pla;
  ccrun's parser: tools/ccrun's awk.
- **The link's routing of names:** `read_summary()` src/link.c:627;
  `resolve()` src/link.c:669-720 (173 at :698-700, 172 at :701,
  `__start` at :712-716); `undefined()` :334; the G_ flags :177-178.
  Step 1 reads every summary, then `resolve()` runs (src/link.c:1757-
  1759).
- **The linker's object names:** `bases[MAX_OBJS]`, `LIST_BYTES` 512
  and `list[]` (src/cclink.c:31-38), `add_base()` :60, `read_list()`
  :68; `MAX_OBJS` 32 (src/link.h:5); objects per program today: ccpp
  28, cccomp 28, ccgen 27, cclink 23 (tools/out/*.objs).
- **Files on the VMs:** FILEIO's open takes a free 1K I/O buffer
  (src/libsrc/apple/fileio.pla:241; lib/fopen.c's accounting);
  `sysgetfileinfo` is unimplemented on plvm
  (src/vmsrc/sysio.c:251-253).
- **The fits:** ccpp 740 and cccomp 724 bytes to spare running; the
  linker about 13K (doc/measure.txt).

## 9. Commit plan (all seven steps done)

Each commit: `make test` and `make bootstrap`; `make measure` after
any size-affecting one, committing doc/measure.txt (and
doc/limits.md) with it. Stage files by explicit path (the
leading-comma temp-file convention, AGENTS.md).

1. **Done** (2026-11-13). **One exit** (section 5.5): init sets TMP; lib/exit.c's plvm path;
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
4. **Done** (2026-11-13). **Use `-L`, and delete tools/cclink:**
   measure.py (its library's map made with tools/libmap.py, linking
   with `-L`, and `PROGRAM.objs` written from the listing) and
   bootstrap.sh (each stage linking with `-L lib`, the stage's
   `lib.map`, `PROG.objs` the program's own objects) link with `-L`;
   tools/a2disk writes `LIB/LIB.MAP` and its usage example becomes
   `CCREL -L LIB -O PROG PROG` (CCLINK after step 5); lib.sh's quit
   test links with `-L` too. Then tools/cclink and its Makefile
   dependencies go. On MAME's 64K IIe, a program compiled there by
   the four passes and linked with `CCREL -L LIB -O T T` prints its
   output and gives `ERR:$03` from `exit(3)` five calls deep, twice,
   and is byte-identical to the host's link; on the 128K IIe too.
   (This check first found section 5.5's SAW bug.)
5. **Done** (2026-11-14). **Rename ccrel to cclink** (section 1's
   list).
6. **Done** (2026-11-14; section 3.1 has the measurements). **ccpp's
   CMDSYS ids leave the pool** (section 3.1), with today's
   `__` spelling: a pure saving, measured on its own, with
   `MAX_PP_POOL` lowered.
7. **The `__cmd_` spellings, the spare CMDSYS ids, the `_` private
   names, and the two checks** (sections 3.1a, 3.2 and 4), with
   `NAM_VERSION` raised and the regenerated pp `.nam`
   expectations and inc/ptest.h. With step 6 done, the prefix itself
   is one string in ccpp.

   **Done** (2026-11-14), with the diagnostic (section 7, item 2:
   yes). The 25 spellings and the 4 private names were one
   substitution each, by word boundary, in every tracked file but
   approach.md, this file and doc/bootstrap-test.md (history; R3 has a
   note). The prefix is fixed.h's `CMD_PREFIX`, for ccpp and cclink
   both; ccpp's ids 26 to 40 are spare (a `?` line each), `main` is
   41, and a file's own names start at 49; `NAM_VERSION` is 3. cclink
   gives 300, "not a CMDSYS function: '__cmd_strcp'", for a plain
   undefined `__cmd_` name, and 301, "only CMDSYS may define a __cmd_
   name", for a definition; tests/errors/cmd_typo.c and cmd_define.c
   check them. Two things the plan got wrong:
   - **Every `.tok` changed, not only the `.nam`** (3.2 said the
     token streams wouldn't): the spare ids move a file's own ids by
     15. The pp tests' expectations were moved by a rule (every id
     from 26 up gains 15, in `$04`, `$07`, `$09` and the end stamp's
     count, and in the comments' "id N"), not copied from ccpp; the
     other numbers in comments were checked by hand. One comment was
     already wrong: funcmacro.tok's x1 and y were ids 45 and 46, not
     43 and 44.
   - **Two tests defined a CMDSYS name** (3.2 said none did):
     tests/run/dead_store.c's `__call` was its old `call`, renamed
     with decision 35, and is `call` again; tests/link/main_second
     tested the old rule, a program's own `__toupper` used instead of
     the import, and now uses `upcase`, with cmd_define.c testing the
     new rule.
   The shared-name byte counts that tests pin grew 4 bytes a CMDSYS
   name (link/basic 80 to 92, link/probe 67 to 79, cclink.sh's nofit
   2991 to 2999, still too few for its static name).

   **Size** (running spare on a 64K Apple II): ccpp 868 to 832 (the
   longer prefix and the spare ids' lines: code 17 bytes, strings 19);
   cccomp 724, unchanged; ccgen 2377 to 2361 (its four helper names);
   cclink 11628 to 11490 (the two checks and the longer names). ccpp's
   pool peak, compiling ccish, rose 37 bytes to 7942 of 8207, since
   ptop.c now sees fixed.h's three new macros.

## 10. For the implementer: how the steps were worked through (done)

All the steps are done, so nothing here is pending. It's kept as a
checklist for work like this: what each step had to show before it
was committed. Read AGENTS.md first: its rules applied to every step.
These are the ones that mattered most here.

**Before you start a step:**

- **Ask the user the step's pending choice, and wait for the answer.**
  Step 2 needs the letters for the driver's and ccgen's `-d` and `-i`
  (section 7, item 1: proposed `-m` and `-r`). Step 7 needs the
  answer on the "not a CMDSYS function" diagnostic (section 7, item
  2). Don't guess.
- **Check the step's facts against the source.** The line numbers here
  are from 2026-11-13 and move; find each place by the function or
  name given, not the number alone. If the code doesn't match what
  this plan says (a function works differently, a name isn't where it
  should be), stop and tell the user what differs, before changing
  anything.
- **Do one step per commit**, in order. Don't start the next step
  until the last is committed. Don't combine steps, and don't do work
  from a later step early.

**While you work:**

- **Size matters most.** ccpp and cccomp have about 740 and 724 bytes
  to spare running, on a 64K Apple II. Any change to code that's in
  ccpp, cccomp or ccgen (src/, lib/) costs or saves bytes there:
  measure it (`make measure`, or `make test`, which runs it) and
  report the change in each program's running spare, from
  doc/measure.txt's summary at its end, in the commit message. If a
  step grows ccpp or cccomp more than this plan estimates, stop and
  tell the user before committing.
- **Expected outputs are worked out by hand, never copied from the
  compiler's output** (AGENTS.md, Testing). The one exception: a
  name file's checksum, which the test scripts compute. When a step
  regenerates expectations (step 7's `.nam` files), check a few by
  hand, and say which.
- **Every function has a comment directly above it**, every message's
  text is in ccish.msg, and every new limit's `#define` gets a
  `limit GROUP:` comment (AGENTS.md, Code style). `make test` checks
  the first two.
- **Stage files by explicit path** (`git add FILE...`), never a
  directory or `-A`: files whose names start with a comma are the
  user's temporary files and must never be committed. Never push.
- **Run MAME only through tools/a2cmd** (or tools/a2screen and
  tools/a2time), never directly.

**Before each commit, all of these must pass:**

1. `make test` (it prints `passed: N tests`; N must not drop, and
   should grow by the step's new tests).
2. `make bootstrap` (it must end `stages 1, 2 and 3 are the same`).
3. The step's own checks, below.
4. The docs the step's section names are updated, and doc/measure.txt
   and doc/limits.md (which `make test` regenerates) are committed with
   the change.

The commit message says what changed and why, the measured size
change, and ends with the two trailer lines AGENTS.md gives.

**Each step's own checks:**

- **Step 2 (options):** each program's usage line matches section 2's
  usage (as the user approved the letters); no program has two options
  that differ only in case; `grep -rn "ccish -l\|cclink -l\|ccgen -d\|ccgen -i\|ccish -d\|ccish -i"` over
  the Makefile, tools/, tests/ and doc/ finds nothing left.
- **Step 3 (`-L`):** section 5.4's tests, each one; tools/libmap.py
  fails on a name two objects define (test it); lib.sh's nostart and
  noexit cases still pass unchanged.
- **Step 4 (use `-L`, delete tools/cclink):** `tools/out/*.objs` lists
  the same objects as before (the order may differ); `make apple`
  writes `LIB/LIB.MAP` on the CCLIB disk; then, on MAME's 64K IIe
  (tools/a2disk's header shows the commands; give the passes a scratch
  copy of cclib.po, deleted after), compile and link a small program
  that calls `exit(3)` from a nested call, with `-L LIB`, and check it
  prints its output and `ERR:$03`.
- **Step 5 (rename):** `grep -rni ccrel` outside `tests/out`,
  approach.md and this file (history stays) and comma files finds
  nothing; MAME's
  64K IIe check from step 4 again, with CCLINK.
- **Step 6 (ccpp's pool):** ccpp's running spare grows (section 3.1
  estimates about 250 bytes); `MAX_PP_POOL` is lowered by the pool
  bytes saved, and doc/limits.md shows the new peak; every pp test's
  `.i` and `.nam` are unchanged (this step changes no output).
- **Step 7 (`__cmd_` and the rest):** no `__put`, `__memcpy` or other
  old CMDSYS spelling is left outside `tests/out`, approach.md and
  comma files (`grep -rn "__\(put\|get\|call\|syscall\|heap\|mem\|str\|toupper\|sext\|isu\)"`, then check
  each hit); `NAM_VERSION` is raised; the two new checks each have an
  errors test; README.md's hello example compiles and runs.

**When something goes wrong:** don't revert or drop tested, working
work on your own judgment, and don't weaken a test to make it pass.
Stop and tell the user what failed, with the output.

