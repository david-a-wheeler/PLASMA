# Working notes for ccish (for coding agents)

What a new session needs to know to carry on working on ccish the way
it has been worked on: the goal, the user's working rules, how to test
and measure, verified VM facts, and where things stand. The design
itself, and the reasons for it, are in approach.md; this file points
there rather than repeating it. (Written 2026-10-31, from the memory
files of the sessions so far; brought up to date 2026-11-12.)

## The goal

- ccish is a compiler for C-ish (a growing subset of C) that emits
  PLASMA VM modules. PLASMA VM runs on 6502 8-bit computers.
  The compiler lives in `ccish/`, currently on the git branch `c-compiler`.
- **The real goal: self-host on a 64K Apple II**, with 2 to 4 floppy
  drives of 140K, beside ProDOS, the VM, the zero page, the stacks and
  the text screen. "32K per program" was only a proxy for that.
- The pipeline is 4 programs: ccpp (preprocess: NAME.c → NAME.i token
  stream + NAME.nam names), cccomp (front end → NAME.ir), ccgen (back
  end → NAME.obj + NAME.syms), cclink (link → REL module). `ccish` runs
  all four on the host.
- **Size matters most for cccomp**, the tightest fit: PLASMA's loader
  reads a whole module file into memory, and cccomp's REL module is
  bigger than the 41,171 bytes free on a 64K Apple II, so it can't
  load that way. The way past the load limit, chosen 2026-11-11, is
  XLOAD, a second-stage loader ccish ships, for images cclink writes
  with `-x`: doc/xload.md (the design and plan;
  doc/streaming-options.md has the analysis behind the choice). Each
  program's "on a 64K Apple II, through XLOAD" lines in
  doc/measure.txt give its need as an image, loading and running, and
  tests/special/fit.sh (in make test) fails if one doesn't fit either
  way; the summary at the report's end has both, and XLOAD's own.
  Running is the tight one: ccpp and cccomp have under 1K to spare
  each, as lower bounds. ccpp's zero-filled tables are in its zero
  region, so raising its limits costs that margin.

  ccgen has headroom; it is a streaming filter with fixed tables, so
  making it bigger doesn't reduce what it can compile.

## Where things are written down

- `approach.md`: the design, every decision and why ("Decisions so
  far", numbered issues, milestone notes, "X design" and "X is done"
  sections with measurements). Read the relevant section before
  changing anything. Issue 24, "Putting cccomp on a diet", lists the
  ways to cut its running memory. Earlier work: "Large zero-filled
  data on the heap" (REL modules only), "Loop design" (steps 1 and 2,
  the DLW fix, "Possible improvements to the loop forms"), issue 20
  "Writing fast C-ish".
- `doc/diet.md`: instructions for making ccish's code smaller (and
  faster at no cost in size), for an agent to follow again and again:
  what has worked, what bites, and how to measure and test each gain.
- `doc/xload.md`: XLOAD and its image format, what's left to do, and
  the plan; `doc/streaming-options.md`: why XLOAD, and the options
  set aside.
- `doc/linking.md`: the record of the linking decisions, all done
  (approach.md decision 46): the final pass's name, cclink, the -v
  flag, the __cmd_ spellings, the C library's one-underscore private
  names, and the -L/lib.map library resolution, with the options,
  rationale, measurements and what the plan got wrong. Read it for
  why, before changing the final pass's options or the __ names.
- `OBJECT.md` (object, IR file and module formats, versions), `IR.md`
  (each IR op), `CCPP.md` (the preprocessor and its files), `README.md`
  (the language as users see it), `../doc/Bytecodes.md` (the VM).
- Source comments: each file's header comment describes its design.

## How the user wants the work done

- **Stay in ccish/:** do not modify or check into git anything
  outside of ccish/ and its descendents without human approval. The
  git repository's root is one level up (PLASMA/), so git run from
  ccish/ acts on the whole tree.
- **Discuss design before coding.** For a new design question, give the
  options with pros, cons and numbers, and a recommendation; then wait
  for "go ahead". Record the outcome in approach.md ("decided in
  discussion", the date; "possible, not committed" when so labelled).
- **Correct C first.** Don't trade C conformance for size without
  saying so; supported constructs are C-compatible, and ccish is a
  documented subset. Cite C11 clauses where a choice rests on them.
- **Self-hosting needs first:** implement what ccish's own source needs
  before "goodies"; say whether a feature is needed for self-hosting.
- **Always consider space**, code and data: `char` where values fit,
  parallel arrays (one per field) rather than arrays of structs, fixed
  tables sized about 10% over measured peaks. Flag space costs up front.
- **Better generated code is worth some compiler size,** but measure
  and report the trade-off, and ask; **never revert or drop working,
  tested work on my own judgment** (the user stopped one such revert).
  When a change grows a program more than estimated, say so plainly.
- **Removing operations that have no effect is welcome** (it cuts space
  and time), as are faster and smaller loops ("loops are typically
  what matter for speed").
- **Verify claims against the source; say plainly when an earlier
  statement was wrong**, and fix the docs. The user catches mistakes
  (e.g. "!= should be exactly the same for signed and unsigned").
- **Commits:** commit each completed step (tested, docs updated); the
  user asks to "commit and continue" for multi-step work. **Never
  push.** End each message with exactly these two lines, last:

      Assisted-by: <agent> (<model>)
      Signed-off-by: <git config user.name> <<git config user.email>>

  The first line names the agent doing the work and the model it runs
  on. Each example is for the agent it names, not a general rule:
  Claude Code writes `Assisted-by: Claude Code (claude-opus-5-5)`
  (updating the model name when the model changes); pi writes
  `Assisted-by: pi (<model>)`, for example `Assisted-by: pi
  (qwen3.8-27b)`. Any other agent does the same with its own name and
  model. The second line is the git identity of whoever is working;
  on this machine that is `Signed-off-by: David A. Wheeler
  <dwheeler@dwheeler.com>`. (Not the Co-Authored-By trailer.)
- **A file named with a leading comma is a temporary file**, by a
  long-standing convention, anywhere in the tree (`src/,parse.c`,
  `doc/,instruction`, `,issue-rld-streaming.md` at the top). Don't
  commit one automatically: the user decides when one is checked in
  (a few are). Stage files by explicit path, not with `git add` of a
  directory or `-A`, which would sweep them in. Don't touch or delete
  the user's comma files or editor swap files. Name your own
  temporary files with a leading comma too.

## Code style

- The small build is `-DCCISH_SMALL`; code tests feature macros (build.h:
  CCISH_LISTING, CCISH_CHECKS, CCISH_TEXT, CCISH_STATS, CCISH_FAST),
  never CCISH_SMALL itself. The full build has CCISH_BIG_LIMITS
  (larger MAX_TYPES and MAX_VARS); it is the small build's limits
  that must fit on the Apple II. At one time we limited ourselves to C89 or C99, but that
  was really a placeholder to enable bootstrapping. We now test
  bootstrapping separately with `make bootstrap`
- **2-space indents, never tabs**; continuation lines align under the
  opening parenthesis or operator.
- **Shared variables: direct access, not accessors** (decided in
  discussion, 2026-11-08: "Merely getting stuff to *fit* is a
  challenge"). A variable other files need is defined in its owner
  .c file and declared `extern` in that file's own header, and the
  others read and write it directly: an access is one 3-byte op, where
  an accessor adds a call, a body and a 5-byte def-table entry. A rule
  about who may change it goes in a comment by the `extern`. Keep a
  function (or, with one caller, a macro) only where the access does
  work. Two limits shape this: every `extern` a file can see counts
  against cccomp's `MAX_VARS` when ccish compiles itself, so declare
  it in the owner's header and include that only where it's used; and
  each distinct array type takes a `MAX_TYPES` slot, so give an
  `extern` array its size where the size's macro is visible.
- **Split headers are NAME.SCOPE.h** (decided in discussion,
  2026-11-08). When some of a file's declarations would cost files
  that don't use them (cccomp's `MAX_VARS` and `MAX_IDS`, ccpp's macro
  tables), they go in a header of their own, and together the headers
  are one "virtual" header: the main one has the usual name, NAME.h,
  and each special-scope one is NAME.SCOPE.h, as diag.h and
  diag.src.h (the source position), and obj.h with obj.fmt.h and
  obj.sym.h. Use a period, not a hyphen: ProDOS names allow only
  letters, digits and periods, 15 at most, starting with a letter.
  Each header's comment names the others it goes with, so a
  maintainer finds them all.
- **Every function has a comment directly above it** (1-2 lines, what
  it does), including one-line static helpers; with `#ifdef` versions,
  one directly above each. Each peephole pattern in peep.c has a
  comment. `tests/special/comments.sh` checks the functions (`make test`
  runs it).
- Messages: every message text is in `ccish.msg` (NUMBER TEXT); code
  writes `error(N /* "text" */, ...)`; `tests/special/msgs.sh` checks them.
- **Enum values are assigned for the set tests and may be reassigned
  freely** (the _Static_asserts are the contract, not the numbers).
  A set test or value manipulation on one (IR ops, tokens, types,
  flags, pseudo-ops) is a #define with a _Static_assert: in the .h
  next to the enum when more than one file uses it, file-local in
  the .c when one does (each macro used costs the self-hosted build
  a MAX_IDS or MAX_VARS entry); inside the #define, name the anchor's
  enum value where reasonable. A comparison with a named constant
  (tok == TC_IF) is fine; a raw number in a bit op, or arithmetic on
  named constants, outside a define + assert pair is not. Machine
  opcodes, stream codes, record tags and character codes are fixed
  by the VM, the format, and C, and their asserts pin the properties
  the code relies on. tests/special/enumvals.sh (in make test) checks.
  See doc/enums.md.
- Prose in the docs: plain, direct sentences; numbers given.

## Testing

- `make` (or `make test`) builds both builds, plvm and the cross-test
  references, and runs every test that is stale, with both builds
  (driven entirely by the Makefile: a wildcard finds each test's input,
  a % rule runs it and writes a stamp that exists only while it passes,
  and the stamp depends on the input, the expected result, the passes
  and the scripts, so a change to any of those re-runs it; `make all`
  and `make small` build without testing). It requires the small build
  to write byte-identical objects and modules.
- `make bootstrap` compiles ccish's sources with ccish itself, in
  three stages, and passes only if the modules are byte-for-byte the
  same: the strongest correctness check, so run it after a change to
  any pass; `make full-test` is the regular suite plus the bootstrap.
- **The kinds:** run (compile and run: stdout, stderr, the exit
  status, listing directives), img (each run test again as an XLOAD
  image, run through XLOAD under plvm, tests/imgrun.sh; a test that
  can't apply says `// img-skip:` and why), gen (the machine ops after the
  peepholes, and a run), link (several files into one program),
  errors (must fail with the message), xrun (the same source run
  under ccish and a host compiler, against the .ref that make xref
  builds; inc/ptest.h is the portability contract), and the special
  scripts (source properties). Each is a directory under tests/,
  driven by the Makefile's wildcards and % rules, which say the
  rest.
- **Expected outputs are worked out by hand from C semantics, never
  copied from the compiler's output.** (Watch 16-bit overflow: an int
  is 16 bits; 65432 prints as -104. Several of my expected values were
  wrong that way; the program was right.) The one exception: the
  `.nam` checksum, which the test scripts compute with awk.
- Run tests (`tests/run/NAME.c` + `NAME.expected`) can check the
  listing: `// listing-has: TEXT`, `// listing-lacks: TEXT`,
  `// listing-count: N TEXT`. TEXT is a plain substring (`grep -F`), so
  `DLW` also matches `ADDLW`: write two spaces, `// listing-count: 19
  DLW`, to make the text ` DLW`. Count exactly, and say in a comment
  where each counted op comes from. For an optimization, test both
  sides: what must be transformed (with exact counts) and what must not
  be (a separate file with `listing-lacks`), with outputs that would
  differ if it went wrong.
- Link tests: `tests/link/NAME/{a,b,...}.c` plus `expected` (run
  output) or `expect-error` (message text). Error tests:
  `tests/errors/NAME.c` plus `NAME.stderr.expected`, whose text the
  compiler's stderr must contain. A run test may also have a
  `NAME.stderr.expected`: the exact stderr, or none for silence. The
  expected line numbers point into the source, so the blank lines where
  the `// expect:`/`// stderr-has:` comments used to be keep the line
  numbers right; don't tidy them away.
- A function has at most `ENTRY_LABEL` ordinary labels (doc/limits.md),
  and every loop now takes 4: split a test with many loops into
  functions.
- **Limits:** mark each user-facing limit's `#define` with a comment
  starting `limit GROUP:` (pp, file, function, link or other), with
  no hand-written peak: doc/limits.md shows the measured ones, from
  doc/measure.txt (to measure a new one, have tools/measure.py report
  it in its "Peaks:" section). `make all` (or `make limits`)
  regenerates doc/limits.md, which is checked in: commit it with the
  change to a limit.

## Measuring

- `make measure` (`tools/measure.py`, about 4 seconds) compiles
  ccish's own sources with ccish (small build) and reports each file's
  code, data and strings, each program's totals and module file, table
  peaks, stack depth, and its fit on a 64K Apple II, as the XLOAD
  image that runs there (its `APPLE_LINK`). The report is
  doc/measure.txt, checked in; `make test` refreshes it (and
  doc/limits.md) after a change, so commit it with the change. It
  writes `tools/out/PROGRAM` (the REL module), `PROGRAM.lst` (cclink's
  listing of it), `PROGRAM.named.lst` (the same, with every static
  function named: for tools/fsize.py), `PROGRAM.apple` (the image), and
  `PROGRAM.objs`, the objects linked, the program's then the
  library's, which measure.py writes from cclink's listing (the link
  is with -L).
- **Where the frame stack goes:** each program's report in
  doc/measure.txt gives its worst-case frame stack (a lower bound),
  each recursion group's functions with their frames, and "the worst
  frame chain": the calls from `(init)` that make that bound, one
  function or recursion group a line, with its frame bytes. To shrink
  the frame stack, shrink what's on that chain (a recursion group
  counts the sum of its functions' frames); `git diff doc/measure.txt`
  after a change shows which lines moved. The report is
  deterministic: the same tree gives the same file.
- `python3 tools/listings.py` (or `make listings`; after `make`)
  writes tools/out/lst/NAME.lst for every source file of the programs
  and the library: each function's comment and C, then the machine ops
  ccgen writes for it (`ccgen -d`, the small build's code, before
  linking). `python3 tools/opstats.py [-p cccomp] [-o]` counts the ops
  in them: each op's share, and the most frequent runs of 2 to 4 ops,
  never across a label: where a peephole pattern or better code
  generation would pay most.
- `python3 tools/loops.py` (after measure.py): accesses to globals moved
  to the heap, in loops, by global and function (REL modules only:
  an image moves nothing to the heap).
- **Compare against the previous commit** with a git worktree at a
  short path (ccish's output names are limited to 128 characters, and
  the scratchpad path is too long): `git worktree add --detach
  /tmp/claude-1000/wb HEAD`, build it, measure both, remove it after.
- **Same output?** `tools/gencmp.sh BASE` (BASE: a built ccish
  directory, such as the worktree above; about a second) compiles this
  tree's sources, every test's and ccish's own, with BASE's passes and
  with this tree's, and compares what each pass writes (.i, .nam, .ir,
  .obj, .syms), then links ccish's four programs both ways (REL and
  -x) and compares those. It names each source and pass that differs,
  and exits 1 if any does. Run it for every change meant to shrink or
  speed up the compiler without changing its output: the tests can
  pass while the output changes (it caught a diet change whose call
  results kept the last argument's flags), so "the same output" is the
  claim to make, and to check, in the commit message.
- **Op counts:** `tools/opcount.py BASE` (BASE: a built ccish
  directory, such as the worktree above) builds a copy of plvm that
  counts each op it runs, compiles every run test with BASE's ccish and
  this tree's, and prints each test whose count changed, any whose
  output differs, and the totals (`-v`: by opcode). Report how many
  tests change, that no output changed, and the totals.
- **Where the passes spend their time:** `make profile`
  (`tools/profile.py`, about 20 seconds) builds a plvm that counts
  ops by bytecode offset, from a copy of ../src/vmsrc patched with
  `tools/plvm-profile.patch` (src/vmsrc itself isn't changed; the
  patch is written so it could go to PLASMA's maintainer). It
  compiles ccish's own sources with ccpp, cccomp and ccgen, each a
  module under that VM, and prints each pass's busiest functions
  (ops, share, entries, ops per entry) in tools/out/profile.txt.
  `PROFILE_ARGS="-f FUNCTION"` adds a function's listing with each
  op's count; `PROFILE_ARGS=cccomp` profiles one pass.
- **Why an optimization doesn't apply:** build a scratch copy of ccgen
  with `fprintf(stderr, ...)` at each rejection, and run it on ccish's
  own sources (as measure.py prepares them in `tools/out/src`). That is
  how we found that cccomp's loops were blocked by calls and returns in
  their bodies, not by their limits.
- **Per-function sizes and frames:** `tools/fsize.py LISTING
  [FUNCTION...]` gives each function's code bytes, ops and frame bytes
  (its ENTER's); `tools/fsize.py -f LISTING`, all of them, the largest
  frame first, which is where to look to shrink the frame stack; and
  `tools/fsize.py -c OLD NEW` compares two listings, each function
  whose size, ops or frame changed. Use `tools/out/PROGRAM.named.lst`,
  which measure.py links with room for every name; in
  `tools/out/PROGRAM.lst`, a static whose name didn't fit is
  `static@$ADDR`, which shifts between builds, and `-c` leaves it out.
  For OLD, run measure.py in a worktree of the earlier commit.

## Verified PLASMA VM facts that shape the code

- **C VM (plvm) and 6502 VMs differ:** DIVMOD's results come in
  opposite orders (never emit it); NATV isn't in the C VM; the C VM's
  SEL searches linearly, the 6502 needs a sorted table; division by
  zero traps on an x86 host (SIGFPE); on an ARM64 one, `a/0` is 0 and
  `a%0` is `a`, and it goes on.
  `NOT` gives $FFFF on both now (checked 2026-11-07, plvm.c); it once
  gave 1 on the C VM. C's `!x` still needs `NOT ANDI 1`, for 1.
- **No SWAP.** Stores are `( value address -- )`, address on top.
- DIV truncates toward zero, MOD takes the dividend's sign, SHR is
  arithmetic: right for signed int, so x/2^n is never SHR for signed.
- Ordered comparisons, and the counted-loop ops (BRGT, BRLT, INCBRLE,
  ADDBRLE, DECBRGE, SUBBRGE, which leave both operands), are **signed**.
  BREQ/BRNE pop both, and have no sign. **Never turn `!=` (or `==`)
  into `<` or `>`**, not even with an "otherwise it overflows, which is
  undefined" argument: they differ for unsigned and pointers.
- `DLB` keeps the stored byte (TOS & $FF); `DLW` keeps the word.
- Eval stack: 32 entries (Apple `ESTKSZ` $40, plvm 32). The 6502
  hardware stack: each bytecode CALL holds 4 bytes until it returns, so
  about 50-60 call levels in all.
- Cycle costs (plvm02.s, without dispatch): LA ~29, LLW ~40, LAW ~53.
  MUL is a 16-step loop; SHL a looping shifter; `DUP ADD` beats
  `CN 1 SHL`.
- PLASMA functions return exactly #n values; CMDSYS puts, puti, putc,
  putln, memset and the like are #0. CMDSYS memset fills with a 16-bit
  pattern (same as C only for 0). Exported names are uppercased, 16
  significant characters.
- Loader: it reads the whole module file into the heap, applies the
  RLD, frees RLD and ESD, then runs init; after init it frees init's
  code (and anything above). The heap is stack-like; modexec loads,
  runs and frees a module. Init returning negative is an error.
  plvm's `heapalloc` stops the VM when full; the Apple's returns 0 (and
  can wrap past $FFFF): check `heapavail` first.
- `xplasm -O` folds constant comparisons and `not` to 1, not $FFFF (a
  bug in the cross-compiler).
- Opcodes $C6-$FE are free; new ones would need 7 VMs and 3 JITs
  changed (approach.md issue 21). The wish list (unsigned compares,
  step-and-branch-if-not-equal) is in irin.c and approach.md.

## Tools on the host

- plvm: `make -C src plvm`; run a module from its directory as
  `/path/src/plvm name` (lowercase, no extension); output is CR LF.
  `-t` traces. "Eval stack over/underflow" means unbalanced code.
- xplasm: `xplasm -AMS file.pla > out.a` (`-S`, or it writes next to the
  source), then `acme --setpc 4094 -o mod out.a`. The test build needs
  both, for the ccstat driver (tests/out/ccstat).
- Exit status: plvm always exits 0. `tools/ccrun [-t secs] name args`
  runs a module (from its directory) through ccstat and exits with
  `main`'s status. Every test that runs a module checks it: the run
  and gen tests against `NAME.status` (else 0), the link tests against
  `tests/link/NAME/status` (else 0), xrun tests require 0, and the
  special scripts run modules with `run_module` (tests/lib.sh), which
  fails on a nonzero status. Work an expected status out by hand (or
  with gcc), as for outputs; a gen test that returns `x == k` exits 1
  when it's right.
- MAME emulates an enhanced IIe, 64K or 128K, with ROMs in ../mame (never
  commit them). Run it only through ccish's guarded scripts, which
  never open a window or take the keyboard and always end:
  tools/a2screen (boot, print the screen), tools/a2time (time a
  program), tools/a2cmd (boot samples/boot.po, a PLASMA disk that
  stops at its prompt, with a second disk, type commands, print the
  screen; A2CMD_DISK3 adds a third drive, A2CMD_128K makes it a 128K
  IIe, which runs PLASMA's 128K VM with its JIT, in 80 columns; type a
  throwaway command first there, as keys typed while the JIT loads
  are lost). make apple (or tools/a2disk, which first runs make apple-inputs, so
  tools/out is never stale) makes the
  disks that run ccish there: boot.po (with the headers), ccish.po
  (XLOAD and XLOAD2, the four passes as images, the messages), cclib.po (the
  library, and room for a program's files). Disks are made with AppleCommander
  (../bin/ac-linux-aarch64-15.2). Leave a load time to finish before
  the next command (A2CMD_GAP): keys typed meanwhile are lost. CMD
  uppercases the command line. The user owns Apple IIs and prefers an
  Apple II+ or IIe.
- Shell commands: keep each call simple and plainly safe (one command,
  `git -C` and `make -C` rather than `cd` chains; files written with
  the editor tools).

## Where things stand, and what's open (2026-11-12)

A snapshot as of the date in the heading; the numbers that change
with every commit (sizes, fits, test counts) are in doc/measure.txt
and make's output, and recent work in git log and approach.md.

Done recently (see git log, approach.md and doc/xload.md): XLOAD and
cclink -x; zero-filled globals, static locals and all-zero initializers
in the zero region; the passes taking options in either case (CMD
uppercases); make apple's disks. On MAME's 64K IIe, the four passes,
as images through XLOAD, compiled ccish's own link.c to an object
byte-identical to the host's (ccpp 34 minutes, cccomp 8.3, ccgen 8.9,
at 1 MHz), and on the 128K IIe too, faster with its JIT (ccpp 23
minutes). exit() unwinds on the Apple (lib/exit.c); the same module
under plvm prints ccstat's marker and stops plvm (doc/linking.md
5.5). The linking work is done (doc/linking.md, decision 46): the
final pass is cclink, it finds library objects with -L and the
library's lib.map, and CMDSYS functions are spelled `__cmd_puts` and
so on.

Open, roughly in the order they matter:
- **Running margin**: ccpp and cccomp have under 1K to spare each
  (doc/measure.txt). Ways to cut cccomp: approach.md issue 24.
- **XLOAD's own room**: its second part fits in cccomp's zero region
  with about 440 bytes to spare (doc/xload.md, section 6).
- **ccpp's speed**: 34 minutes for link.c at 1 MHz, four times
  cccomp's.
- **A ProDOS file type for images** (BIN for now): the user's choice.
- **The other PLASMA platforms** (doc/xload.md, R6; leave the partial
  C64 work in src/vmsrc alone).
- **A real 64K Apple II**: run ccish there, and measure its free
  memory (E1; MAME's is in doc/measure.txt).
- **The 6502 stack depth** of cccomp's recursion (approach.md 18.5).
- Loop improvement 3: calls that can't recurse in a counted loop's
  body (CMDSYS functions, helpers, earlier leaf functions): **the
  user's decision; "no calls" stands until then.** cccomp's loops
  with calls are mostly blocked by calls to its own non-leaf
  functions.
- Loop improvements 4 and 5's rest (array stores in nested counted
  loops; global or char loop variables), and counting `!=` loops if
  the VM gains INCBRNE.
- Trim candidates in ccgen: peep.c's `frame_expr()` (a chain of op
  tests a table could shorten), the DLW-fix generalization, and
  irloop.c's loop code generally.
- A dead store before a call is kept (the scan stops at calls); frames
  aren't shrunk when a local's stores all go.
- `HEAP_SPARE` (the bytes the init function leaves main's frames)
  was a guess; XLOAD's 512 for frames is the same guess.
- `CCISH_FAST`: a faster `find_func()` waits behind it for when size
  matters less.
