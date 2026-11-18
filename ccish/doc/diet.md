# Putting ccish on a diet: instructions for an agent

These are instructions for you, an AI agent (or a person), asked to
make ccish's code smaller, and faster where that costs no size. They
can be given again and again, to many agents in turn; each run should
leave the tree a little better, with every gain measured and every
change tested. Read AGENTS.md first: its rules apply here, and this
file adds to them.

## The goal

ccish's four passes (ccpp, cccomp, ccgen, cclink) must run on a 64K
Apple II, and they have very little memory to spare while they run:
see the summary at the end of doc/measure.txt ("And their fit on a 64K
Apple II"). Make the final build of ccish, as it runs on an Apple II,
more efficient:

1. **Size first.** Less memory while a pass runs on an Apple II
   (its code, data, zero region, def table and frame stack), and smaller
   files on disk. **ccpp matters most** for now since it's really slow
   and we can't improve that without 2K more memory free. cccomp is also
   short of space, while
   ccgen and cclink have more room, but every byte saved anywhere helps.
2. **Speed, when it costs no size.** The passes are already very slow on a
   1 MHz Apple II (ccpp, the slowest, most of all), so never
   make them slower; a change that makes them faster at no cost in
   size is very welcome.
3. **Don't make the code much harder to understand.** Comments and
   clearer names can explain a less obvious form. Don't trade C
   correctness for size (AGENTS.md, "Correct C first").
4. **Correctness most important.** Do not sacrifice correctness.
   Pass tests. Add mechanisms like tests and `_Static_assert`
   as appropriate to ensure that invariants are kept without adding
   to memory size of final execution time.

Expect gains of tens of bytes at a time, sometimes a few hundred. They
add up: one round took cccomp from 724 bytes to spare to 1,183
(git log, 2a391a6f to 0bb0d411).

## Two kinds of change

**A. The passes' own source** (src/, and lib/ for what they link):
smaller or faster source for the same work. The code ccish writes
must not change at all: tools/gencmp.sh (below) must report "the same
output" against the tree before your change.

**B. The code generator** (cccomp's IR choices; ccgen's lower.c,
peep.c, emit.c, irloop.c): better code for every program, the passes
themselves included, since ccish compiles itself. Here the output *is*
meant to change, so gencmp.sh reports differences, and the checks are
the tests, the bootstrap and the measurements: every change must keep
every program correct and make the code smaller (or faster at the
same size) overall.

Say in each commit message which kind it is.

## Where the memory goes

doc/measure.txt starts with a list of its parts; search for the text
it quotes. For each program ("cccomp:" and the others), the "on a 64K
Apple II" lines give what it needs while running: the def table (5
bytes for each function), data, the zero region (zero-filled globals),
code, the frame stack at its worst, FILEIO, and I/O buffers. Each of
those is a place to save:

- **Code**: the largest share. `tools/fsize.py
  tools/out/PROGRAM.named.lst` gives each function's size.
- **Frame stack**: each program's "the worst frame chain" lists the
  calls from `(init)` that make the bound, one function or recursion
  group a line, with its bytes. Shrink what's on that chain. A
  recursion group counts the **sum** of its functions' frames, so a
  frame removed anywhere in it lowers the bound, and one added
  anywhere raises it. The figure is a lower bound: in real use, a
  recursive function's frame is paid again at every level of nesting,
  so a saving there is worth more than it shows.
  We do implement same-function tail recursion optimization into a jump
  to the beginning of the function, so in that *narrow* case
  there is no extra stack frame.
  `tools/fsize.py -f tools/out/PROGRAM.named.lst` lists the largest
  frames first.
- **Def table**: 5 bytes for every function, kept while the program
  runs. A new function costs that, plus its ENTER and LEAVE, plus 3
  bytes a call; merging two small functions saves it.
- **Zero region and data**: tables sized by the limits (doc/limits.md
  has each limit beside its measured peak; size a table about 10%
  over its peak). Those peaks are ccish's own source only: before
  shrinking a limit, measure the peak over all of `tests/`,
  `samples/`, `src/`, `lib/` and `xload/`, in the small build and the
  full build ("How to measure", step 6): three shrinks that fit
  ccish's own source each broke a sample or a test ("Approaches
  checked that don't work"). make test catches such a shrink either
  way: tests/special/small.sh compiles every test source and every
  sample with both builds (rodfast with lores, as samples/Makefile
  links them), so a limit violation in any of them fails the suite.

## Approaches that have worked

Each of these is in git history, with its measurements:

- **Keep big structs out of recursive frames.** cccomp's `Operand`
  is in the frame of nearly every level of the expression parser. A
  copy kept "just in case" (2a391a6f), or an Operand used by one rare
  path in a function that recurses (98421174: `return`'s, moved into
  `parse_return()`), costs its size at every nesting level.
- **Parse into a used-up operand.** Once a condition or a left
  operand has been used, its Operand is free: parse the next part into
  it rather than into a new local (ce3f50eb, 697190f3). Check that
  every field the caller later reads is set; see the warning below.
- **Make small fields `char`.** Flags (0 or 1) and small kinds in a
  struct that's copied often, or sits in many frames (1d0c47bb:
  Operand's seven, 46 bytes to 39; also shorter struct copies, so
  smaller and faster). Check every assignment's range first: ccish's
  `char` is unsigned, it has no `signed char`, and a value of 256
  would become 0.
- **Remove buffers from the deepest point.** An error stops a pass
  from wherever it is, so the error path's frames sit on top of the
  deepest chain in every pass (0bb0d411: report() wrote a message
  straight to stderr instead of through a 96-byte buffer: 94 bytes in
  all four passes).
- **Walk with a pointer, not an index.** In ccish, `a[i]` on a global
  array is an LA and an ADDLW (5 bytes) where `*p` is LLW and LB (3):
  676473b2's table walk.
- **One exit, one copy of shared code.** A function with three
  returns that each repeat the same updates shrinks with one exit
  (676473b2's `pp_intern()`).
- **Sub-blocks share frame space.** Locals declared in a block are
  freed when it ends, and a later block reuses their slots (pstmt.c's
  `parse_block()`). But a function's frame is fixed at its ENTER, the
  largest any of its blocks needs, so only moving the locals to
  another function takes them out of it entirely.

## Approaches checked that don't work

Tried, measured, and backed out. Don't try them again as stated; if
you think a variant avoids the measured cost, say how, before you
expect it to pay.

- **A pointer instead of an Operand copy.** ccish compiles
  `Operand r, x; r = x;` as one `__cmd_memcpy(&r, &x, 33)` call
  (about 9 bytes of code), not 17 field moves, so deleting a copy
  saves less than it looks like, while the pointer costs more to use
  at every place the operand is read. Three tries, all backed out:
  `fold_held()` (a pointer to the held operand instead of the local
  copy `Operand r = *rp`, which its comment defends: "cheaper to
  reach, and off the recursion's frames") grew 30 bytes of code,
  `r` being read at every fold, and running spare went 1506 to 1476;
  `parse_conditional()` (its constant-condition path: parse the taken
  side directly into the destination operand, not a local, saving
  `*o = b`) grew 10 (1506 to 1496); `parse_assign()` (a `Operand
  *res` naming the
  compound assignments' result instead of the two `r = x` copies)
  shrank 3 bytes of code but added 2 of frame, in a function on
  cccomp's worst chain, a wash. The frame local costs its size on
  the worst chain even when only a rare path uses it; the memcpy
  copy, in code, costs what it costs once.
- **Writing straight to a global array instead of a local staging
  array.** ccgen's `run_do()` builds up to three ops in a local
  `int o[3][3]`, then copies them into the global `buf_*` arrays.
  Writing the ops straight into `buf_*` saved 18 bytes of frame but
  grew 26 of code: a store to a global array costs a few bytes more
  than a store to a frame local, and `run_do()` does about a dozen
  stores. Running spare 2529 to 2521; backed out. (The "walk with a
  pointer" gain above is reads in a hot loop; for stores done once
  per call, the local array is the cheaper home.)
- **Sizing a limit to ccish's own peak.** A limit's peak must be
  measured over everything ccish compiles: its own source, the tests
  and the samples (doc/measure.txt's peaks are ccish's own source
  only). Three shrinks in one session each broke one of the other
  two: `MAX_EXP_BYTES` 256 to 154 (`samples/rodfast.c`'s `DRAW_ROWS`
  expands to 232 bytes: "macro expansion too long"),
  `MAX_ARG_SLOTS` 32 to 5 (`tests/run/pp_join.c`'s `SUM6` takes six
  arguments), `MAX_FUNC_CODE` 1280 to 1120 (`rodfast.c`'s `main` is
  1223 bytes of bytecode: "function too large"). Before shrinking,
  run a stats pass over all of `tests/`, `samples/`, `src/`, `lib/`
  and `xload/`, and over the full build too: its lint checks' peaks
  are higher than the small build's (`pexpr.c`: 209 functions to
  197, 329 parameter types to 310). One shrink was skipped on use,
  not size: `MAX_LINE` 255 to 203 would have saved 52 bytes, but the
  limit is on the user's source lines, which C11 asks to run to
  4,000; 255 is already a deviation, and tightening it further for
  52 bytes wasn't worth it.
- **A big static buffer into a frame local.** ccpp's `put_string()`
  keeps its 256-byte spelling buffer static: 256 bytes of zero
  region, paid in every run, for `#param` stringification, which
  nothing ccish compiles uses. Moving it into the frame is
  impossible: ccish's own error 115, "local array doesn't fit in the
  255-byte frame (make it static or global)". A local array of 256
  bytes or more must stay static or global. "Remove buffers from the
  deepest point" (above) has that 255-byte floor.

## Things that will bite you

- **The tests can pass while the output changes.** Reusing the
  caller's Operand for a call's arguments left the last argument's
  `is_bool` in the call's result; the tests passed, and only
  tools/gencmp.sh showed the changed IR. It also measured worse. It
  was backed out. For a kind A change, run gencmp.sh every time.
- **Estimates are often wrong**, sometimes by twice. Measure every
  change; report the real figure, and say plainly when it missed.
- **ccish compiles itself,** so the passes' source must be in the C
  ccish supports: no `signed char`, no `long`, no floating point. Its
  library has no `memcmp` or `strncmp`. A host build that compiles is
  not enough: make test also has measure.py compile the source with
  ccish, and fails if it can't.
- **Limits in self-compilation:** when ccish compiles its own source,
  a file may be at a limit, such as `MAX_VARS` or `MAX_TYPES`:
  doc/limits.md gives each limit's peak and the file that reaches it.
  In a file at its limit, a new global, or a new array type, won't
  fit, and raising a limit costs cccomp memory.
- **Library calls pull in objects.** Calling `putc` in diag.c added
  the library's putc object to ccpp's and cclink's links, past the
  linker's room for the added objects' names (`LIB_NAMES`). Use what
  every program already links (`fputs` through `put_err()`).
- **A comment changes no code,** but measure.txt gives the largest
  source file's size ("parse.c: N bytes of source"), so a comment
  there moves that line; that's fine.
- **Don't trade speed away.** A change on a hot path (`make profile`
  shows which functions run most, such as ccpp's identifier lookup)
  needs a profile showing it isn't slower.

## Ideas not yet tried

- The large frames on cccomp's worst chain (measure.txt's "the worst
  frame chain"; `tools/fsize.py -f`): when this was written,
  `parse_declaration`, `init_elem` and `copy_struct`. (`fold_held`'s
  and `parse_assign`'s Operand copies were checked and don't pay: see
  "Approaches checked that don't work".)
- approach.md issue 24, "Putting cccomp on a diet", lists more.
- For kind B: `tools/opstats.py -p cccomp` counts the commonest ops
  and runs of 2 to 4 ops in the passes' code: a run that's common and
  could be shorter is a peephole pattern (peep.c) or a better choice
  in code generation. `make listings` writes each source file's C
  beside the machine ops ccgen writes for it.
- ccpp's six Bfiles (the source and up to three includes, NAME.i and
  NAME.nam) are all static, but at most four files are open at once
  (measure.txt), and the name file is written after the source is
  done: a little free list could let a finished file's buffer be
  reused, and take one or two Bfiles, 64 or 128 bytes, out of ccpp's
  zero region.
- ccpp's name pool (ppname.c): find's walk was 29% of ccpp's ops
  at the round's start (153,415,063, bool era). Measured first
  (find's own counters, 29 files): of the 6.0 million entries
  walked, 2.5 million are new names' failed finds and 3.3 million
  are old names' cache misses; the walks average a couple of hundred
  entries because the pool grows through the file, 7,202 of its
  8,207 bytes in one file (pexpr.c), around nine hundred entries.
  A jump table authoritative enough to end the walk would be 1,024
  to 2,048 slots of 2-byte offsets, 2 to 4K, most of ccpp's spare,
  so the cuts attack the walks' kinds instead, in four:
  1. A new file's second find (its number, after the add) is gone:
     nfile, the pool's file count, is the number (2 bytes of zero).
  2. A 1,024-bit set (pp_bits, 128 bytes), one bit per name hash's
     low ten bits: a name whose bit is clear is definitely new (the
     bit is set when the name is added, and nothing unsets it until
     the pool resets), so pp_intern and pp_add_file skip the failed
     walk and add at once; a set bit may be a collision's, and the
     walk goes on. File names' bits too. 137 bytes of code, 128 of
     zero, 2 of frame (the frame stack is 236 to 238).
  3. The recent-names cache (a hit is checked against the pool's
     spelling, so it can never be wrong; a miss walks as before):
     64 slots of two names each, 800 bytes, the name hash's low six
     bits, the way of the next. Measured, the slot count buys hits
     with diminishing returns (the entries old names' walks cover,
     29 files: 16 slots, 3,330,216; 32, 2,407,448; 64, 1,693,974;
     128, 1,108,605), so 64, half of 128's cost, is the last step
     that pays; 128 waits for spare.
  4. The walk keeps the entry's address, p, and end, pool +
     pool_used, in locals: the pool's base and pool_used are loaded
     once per find, not per entry (same()'s address and *at, p -
     pool, are in hand), -2 ops a entry; 9 bytes of code, the frame
     stack unchanged. An inlined same() was tried and is worse: the
     compiler re-tests the invariants per char and loses the
     counted-loop shape (26 to 39 ops an entry against 23 to 25);
     the call costs 8 of same's 22 ops, but its char loop, 10, is
     already tight, and register locals buy nothing in the VM.
  The round's result: find, 153,415,063 to 62,154,846 ops, -60%;
  ccpp, 526,637,698 to 430,381,437, -96,256,261, -18.3%; the passes
  together, -13.9%; ccpp's spare, 2,343 to 1,581, 762 bytes. The
  earlier pool cuts (f7997f7b, 22470bdb) stand: the length byte is
  read once per entry, and two names per slot, so a thrashing pair
  keeps both. What didn't pay: the slot table (2 to 4K, and the
  bitmap does 90% of its job in 128 bytes); inlining same() in
  find (the compiler loses the counted-loop shape, 26 to 39 ops an
  entry against 23 to 25, measured in the listing); register int j
  in same() (byte-identical code: the loop is already the
  compiler's counted form). Next, from this round's profile: the
  cache's 128th slot pair waits for spare (585,369 entries for
  640 bytes, 900 entries a byte); pp_mac_find, 44,591,862 ops, 10.4%
  of ccpp now, a macro-table walk per intern, is the next name
  lookup to attack (a per-id macro flag, set and cleared by the
  define and undef directives, would skip it for the ids that name
  no macro); and pp_word's strcmp chain, 41,087,478 ops, 9.5%: each
  word that is no keyword pays up to 46 strcmps (short and _Bool
  prechecked, then the 44 of keywords[]) until the NULL: a first
  char and length prefilter, or the keywords' own small table, would
  cut it.
- Observation, from the BLOCK 128-to-64 round: how many distinct
  types a compilation unit uses depends, by one or two, on the paths
  the name file records for its headers (the name table feeds the
  type table's keys). The limit's 10% headroom covers it; just be
  aware a longer or shorter build path can move a count by a little.

## How to measure

1. **Before you change anything,** make a worktree of the current
   commit, build it, and keep it as your BASE. Make it at a short
   path in the allowed area, at the repo root, with a leading comma,
   like a temporary (AI may create comma files and directories; don't
   commit it). Don't use /tmp, even though it looks like a scratch
   area: a session often runs under nono's OS sandbox, where /tmp is
   write-only, so a worktree there can't be read or built, and one
   left there can't be read or removed:
   `git worktree add --detach /home/dwheeler/projects/PLASMA/,wb HEAD`,
   then `make -C /home/dwheeler/projects/PLASMA/,wb/ccish` (which
   builds, tests and measures it). ccish's output names are limited
   to 128 characters, so the path must be short.
2. **After a change,** `make test` rebuilds what's stale, runs every
   stale test, and refreshes doc/measure.txt and doc/limits.md.
3. **Read the change:** `git diff doc/measure.txt`. Look first at the
   summary at its end, "And their fit on a 64K Apple II": each
   program's Running Spare is the figure that matters (a lower bound,
   marked `*`, when its stack has unbounded recursion). Then each
   program's "running, needs" line, its "frame stack at least" line,
   its "the worst frame chain" lines, and the per-file table at the
   top (code, data, strings and zero data of each source file).
   measure.txt is deterministic: the same tree gives the same file, so
   every line that moved, moved because of your change.
4. **Per function:** `tools/fsize.py -c
   /home/dwheeler/projects/PLASMA/,wb/ccish/tools/out/cccomp.named.lst
   tools/out/cccomp.named.lst` lists each function whose size, ops or
   frame changed.
5. **Speed:** `make profile` (about 20 seconds) gives each pass's ops
   by function, compiling ccish's own sources (tools/out/profile.txt;
   `PROFILE_ARGS=ccpp` for one pass); run it in BASE too and compare
   the totals. `tools/opcount.py BASE` gives the ops each run test
   takes under the old and new compiler: for kind B, the speed of the
   code generated.
6. **Limits, before you shrink one:** doc/limits.md's peaks are
   ccish's own source only. measure.py's stats passes, which `make
   test` builds in tools/out/big/, give one source's table use
   (their table limits are raised where measure.py's BIG_LIMITS says,
   so the true peak is reported, not the limit's error; the rest keep
   the real limit, and a "too large" error says the peak is past it):
   `tools/out/big/ccpp -o NAME src.c`, then
   `tools/out/big/cccomp-stats -k NAME` and `tools/out/big/ccgen-stats
   NAME` (the stats lines come on stderr). Take the most over all of
   `tests/`, `samples/`, `src/`, `lib/` and `xload/`, and check the
   full build too (`python3 tools/measure.py -full`): its lint
   checks' peaks are higher (pexpr.c: 209 functions to the small
   build's 197, 329 parameter types to 310). And if a measure.txt
   line looks as if it shouldn't have moved, the XLOAD numbers are
   the image headers themselves: tools/out/PROGRAM.apple, the
   little-endian words at offsets 4, 6, 8 and 10 are its data, zero
   region, code and functions.

## How to test

Every change, before it's committed:

1. **`make test`** must pass every test (it prints `passed: N
   tests`), with N unchanged unless you added tests.
2. **`make bootstrap`** must end `stages 1, 2 and 3 are the same`:
   ccish compiles itself with itself, three times. Run it after any
   change to a pass.
3. **Kind A: `tools/gencmp.sh /home/dwheeler/projects/PLASMA/,wb/ccish`**
   must end
   `gencmp: the same output`: every test's source and ccish's own,
   through both trees' passes, byte for byte, and the four programs
   linked both ways. If it names a difference, your change changed
   the generated code: find out why before going on.
4. **Kind B:** gencmp.sh will report differences; look at a few, and
   be sure each is the improvement you meant. The gen tests
   (`tests/gen/*.exp`, the machine ops after the peepholes) change with
   the code: work each new expectation out by hand from the C and the
   rule you added, never by copying the compiler's output (AGENTS.md,
   "Testing"). Add a test for the new pattern, and one where it must
   not apply.
5. **If a change touches what only runs on an Apple II** (lib/exit.c,
   XLOAD, an init function, CMDSYS calls), run the MAME check AGENTS.md
   describes, through tools/a2cmd only.

A change that fails any of these is not done. Fix it, or back it out
and record what you learned in the commit message of what you did
instead, or in your report.

## How to commit and report

- **One logical change per commit**, so that any one can be reverted
  alone. Commit each as soon as it's measured and tested.
- **The commit message gives the numbers:** the running spare of
  each program it changes, before and after (from doc/measure.txt's
  summary), what it saved where (code, frame, data), its effect on
  speed if any, "the same output" for kind A (gencmp.sh), and the test
  and bootstrap results. Commit doc/measure.txt and doc/limits.md with it.
- Stage files by explicit path, never push, end with the trailers
  AGENTS.md gives, and don't revert another change that works on your
  own judgment.
- **When you stop,** report each commit and its gain, the total gain
  for each program, what you tried that didn't pay (and why), and the
  ideas you'd try next; add those ideas to "Ideas not yet tried" above.
  Remove your worktree (`git worktree remove`).
