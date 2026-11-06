# Working notes for ccish (for coding agents)

What a new session needs to know to carry on working on ccish the way
it has been worked on: the goal, the user's working rules, how to test
and measure, verified VM facts, and where things stand. The design
itself, and the reasons for it, are in approach.md; this file points
there rather than repeating it. (Written 2026-10-31, from the memory
files of the sessions so far, before a move to a new computer.)

## The goal

- ccish is a compiler for C-ish (a growing subset of C) that emits
  PLASMA VM modules. PLASMA VM runs on 6502 8-bit computers.
  The compiler lives in `ccish/`, currently on the git branch `c-compiler`.
- **The real goal: self-host on a 64K Apple II**, with 2 to 4 floppy
  drives of 140K, beside ProDOS, the VM, the zero page, the stacks and
  the text screen. "32K per program" was only a proxy for that.
- The pipeline is 4 programs: ccpp (preprocess: NAME.c → NAME.i token
  stream + NAME.nam names), cccomp (front end → NAME.ir), ccgen (back
  end → NAME.obj + NAME.syms), ccrel (link → REL module). `ccish` runs
  all four on the host.
- **Size matters most for cccomp**, the one that doesn't fit yet. The
  loader reads a whole module file into memory, so a program's module
  file must fit in free memory (under about 40K, never measured on a
  real machine). Sizes (small build, 2026-10-31, after d10a1a2e):

  | Program | Code | Module file |
  |---|---|---|
  | ccpp | 13,817 | 26,049 |
  | cccomp | 26,101 | 42,743 (still too big) |
  | ccgen | 15,635 | 26,297 |
  | ccrel | 7,713 | 14,200 |

  ccgen has headroom; it is a streaming filter with fixed tables, so
  making it bigger doesn't reduce what it can compile.

## Where things are written down

- `approach.md`: the design, every decision and why ("Decisions so
  far", numbered issues, milestone notes, "X design" and "X is done"
  sections with measurements). Read the relevant section before
  changing anything. Recent work: "Large zero-filled data on the heap",
  "Loop design" (steps 1 and 2, the DLW fix, "Possible improvements to
  the loop forms"), issue 20 "Writing fast C-ish".
- `OBJECT.md` (object, IR file and module formats, versions), `IR.md`
  (each IR op), `CCPP.md` (the preprocessor and its files), `README.md`
  (the language as users see it), `../doc/Bytecodes.md` (the VM).
- Source comments: each file's header comment describes its design.

## How the user wants the work done

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
- **Files in `src/` named with a leading comma** (`,parse.c`,
  `,enum-set-membership`) and editor swap files are the user's own:
  never touch, delete or commit them.

## Code style

- C89, strict: `-std=c89 -pedantic -Wall -Wextra`, no warnings. The
  small build is `-DCCISH_SMALL`; code tests feature macros (build.h:
  CCISH_LISTING, CCISH_CHECKS, CCISH_TEXT, CCISH_STATS, CCISH_FAST),
  never CCISH_SMALL itself.
- **2-space indents, never tabs**; continuation lines align under the
  opening parenthesis or operator.
- **Every function has a comment directly above it** (1-2 lines, what
  it does), including one-line static helpers; with `#ifdef` versions,
  one directly above each. Each peephole pattern in peep.c has a
  comment. `tests/spec/comments.sh` checks the functions (`make test`
  runs it).
- Messages: every message text is in `ccish.msg` (NUMBER TEXT); code
  writes `error(N /* "text" */, ...)`; `tests/spec/msgs.sh` checks them.
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
- A function has at most 104 labels (`MAX_LABELS`), and every loop now
  takes 4: split a test with many loops into functions.

## Measuring

- `python3 tools/measure.py` (8 seconds) compiles ccish's own sources
  with ccish (small build) and reports each file's code, data and
  strings, each program's totals and module file, table peaks, stack
  depth. It writes `tools/out/PROGRAM.lst`, ccrel's listings.
- `python3 tools/loops.py` (after measure.py): accesses to globals moved
  to the heap, in loops, by global and function.
- **Compare against the previous commit** with a git worktree at a
  short path (ccish's output names are limited to 128 characters, and
  the scratchpad path is too long): `git worktree add --detach
  /tmp/claude-1000/wb HEAD`, build it, measure both, remove it after.
- **Op counts:** `tools/opcount.py BASE` (BASE: a built ccish
  directory, such as the worktree above) builds a copy of plvm that
  counts each op it runs, compiles every run test with BASE's ccish and
  this tree's, and prints each test whose count changed, any whose
  output differs, and the totals (`-v`: by opcode). Report how many
  tests change, that no output changed, and the totals.
- **Why an optimization doesn't apply:** build a scratch copy of ccgen
  with `fprintf(stderr, ...)` at each rejection, and run it on ccish's
  own sources (as measure.py prepares them in `tools/out/src`). That is
  how we found that cccomp's loops were blocked by calls and returns in
  their bodies, not by their limits.
- Per-function sizes: `tools/fsize.py LISTING [FUNCTION...]`, or
  `tools/fsize.py -c OLD NEW` to compare two listings (such as
  `tools/out/cccomp.lst` from a worktree and from this tree). Static
  functions whose names didn't fit are `static@$ADDR`, which shifts
  between builds, so `-c` leaves them out: compare per file instead
  (measure.py's table).

## Verified PLASMA VM facts that shape the code

- **C VM (plvm) and 6502 VMs differ:** `NOT` gives 1 on the C VM and
  $FFFF on the 6502 (use `NOT ANDI 1`); DIVMOD's results come in
  opposite orders (never emit it); NATV isn't in the C VM; the C VM's
  SEL searches linearly, the 6502 needs a sorted table; division by
  zero crashes the C VM.
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
  source), then `acme --setpc 4094 -o mod out.a`.
- No Apple II emulator is installed: measuring free memory on a real
  64K Apple II (approach.md 18.6 E1) is still to do. The user owns Apple
  IIs and prefers an Apple II+ or IIe.
- Shell commands: keep each call simple and plainly safe (one command,
  `git -C` and `make -C` rather than `cd` chains; files written with
  the editor tools).

## Where things stand, and what's open (2026-10-31)

Done recently (see git log and approach.md): the ccpp preprocessor;
the header split; C6 (address constants from the expression parser);
large zero-filled globals moved to the heap (option 1); loops with the
test at the bottom; counted loops on the eval stack; the DLW fix (for
any step that stores the variable); global and expression limits;
`return` in a counted loop; pure-expression init folding; dead stores
before a return. 381 tests pass.

Open, roughly in the order they matter:
- **cccomp doesn't fit** (module file 42,743). Size work recorded but
  not done: approach.md's "last 2K" candidates (NAME.i/.ir via stdio,
  enums to #defines), option 3 for zero-filled data (a BSS segment in
  the REL format, preferred once the VM may change).
- **Measure free memory on a real 64K Apple II** (E1).
- **The 6502 stack depth** of cccomp's recursion (approach.md 18.5).
- Loop improvement 3: calls that can't recurse in a counted loop's
  body (CMDSYS functions, helpers, earlier leaf functions): **the
  user's decision; "no calls" stands until then.** cccomp's 22 loops
  with calls are mostly blocked by calls to its own non-leaf functions.
- Loop improvements 4 and 5's rest (array stores in nested counted
  loops; global or char loop variables), and counting `!=` loops if
  the VM gains INCBRNE.
- Trim candidates in ccgen: peep.c's `frame_expr()` (166 bytes, a
  chain of op tests a table could shorten), the DLW-fix generalization
  (+249), irin.c's loop code generally.
- A dead store before a call is kept (the scan stops at calls); frames
  aren't shrunk when a local's stores all go.
- `HEAP_SPARE` (512 bytes left for main's frames by the init function)
  was a guess.
- `CCISH_FAST`: a faster, 12-byte-bigger `find_func()` waits behind it
  for when size matters less.
