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
Apple II"). Make all of the code ccish generates more efficient:

1. **Size first.** Less memory while a pass runs (its code, data, zero
   region, def table and frame stack), and smaller files on disk.
   **cccomp matters most**, since it has the least room; ccpp next.
   ccgen and cclink have more room, but every byte saved still helps.
2. **Speed, when it costs no size.** The passes are already slow on a
   1 MHz Apple II (ccpp, the slowest, most of all), so never
   make them slower; a change that makes them faster at no cost in
   size is very welcome.
3. **Don't make the code much harder to understand.** Comments and
   clearer names can explain a less obvious form. Don't trade C
   correctness for size (AGENTS.md, "Correct C first").
3. **Correctness most important.** Do not sacrifice correctness.
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
  `tools/fsize.py -f tools/out/PROGRAM.named.lst` lists the largest
  frames first.
- **Def table**: 5 bytes for every function, kept while the program
  runs. A new function costs that, plus its ENTER and LEAVE, plus 3
  bytes a call; merging two small functions saves it.
- **Zero region and data**: tables sized by the limits (doc/limits.md
  has each limit beside its measured peak; size a table about 10%
  over its peak).

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

## Things that will bite you

- **The tests can pass while the output changes.** Reusing the
  caller's Operand for a call's arguments left the last argument's
  `is_bool` in the call's result; the tests passed, and only
  tools/gencmp.sh showed the changed IR. It also measured worse. It
  was backed out. For a kind A change, run gencmp.sh every time.
- **Estimates are often wrong**, sometimes by twice. Measure every
  change; report the real figure, and say plainly when it missed.
- **ccish compiles itself,** so the passes' source must be in the C
  ccish supports: no comma operator, no `signed char`, no `long`, no
  floating point. Its library has no `memcmp` or `strncmp`. A host
  build that compiles is not enough: make test also has measure.py
  compile the source with ccish, and fails if it can't.
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

- `stop()`'s copy of a message's detail (diag.c, `MAX_PATH` bytes)
  is the largest frame in every pass. It exists because `clean_up()`
  may reuse `stream_name()`'s buffer; reporting before the partial output
  is removed would make it unnecessary, but cccomp's source-name hook
  (tokin.c) opens the name file through the same buffer.
- The large frames on cccomp's worst chain (measure.txt's "the worst
  frame chain"; `tools/fsize.py -f`): when this was written,
  `parse_declaration`, `init_elem`, `copy_struct`, `fold_held` (two
  Operand copies) and `parse_assign`.
- approach.md issue 24, "Putting cccomp on a diet", lists more.
- For kind B: `tools/opstats.py -p cccomp` counts the commonest ops
  and runs of 2 to 4 ops in the passes' code: a run that's common and
  could be shorter is a peephole pattern (peep.c) or a better choice
  in code generation. `make listings` writes each source file's C
  beside the machine ops ccgen writes for it.

## How to measure

1. **Before you change anything,** make a worktree of the current
   commit at a short path, build it, and keep it as your BASE:
   `git worktree add --detach /tmp/claude-1000/wb HEAD`, then
   `make -C /tmp/claude-1000/wb/ccish` (which builds, tests and
   measures it). ccish's output names are limited to 128 characters,
   so the path must be short.
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
   /tmp/claude-1000/wb/ccish/tools/out/cccomp.named.lst
   tools/out/cccomp.named.lst` lists each function whose size, ops or
   frame changed.
5. **Speed:** `make profile` (about 20 seconds) gives each pass's ops
   by function, compiling ccish's own sources (tools/out/profile.txt;
   `PROFILE_ARGS=ccpp` for one pass); run it in BASE too and compare
   the totals. `tools/opcount.py BASE` gives the ops each run test
   takes under the old and new compiler: for kind B, the speed of the
   code generated.

## How to test

Every change, before it's committed:

1. **`make test`** must pass every test (it prints `passed: N
   tests`), with N unchanged unless you added tests.
2. **`make bootstrap`** must end `stages 1, 2 and 3 are the same`:
   ccish compiles itself with itself, three times. Run it after any
   change to a pass.
3. **Kind A: `tools/gencmp.sh /tmp/claude-1000/wb/ccish`** must end
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
