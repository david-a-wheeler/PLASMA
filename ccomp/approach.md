# C-ish Compiler in Plasma approach

I want to implement a compiler for C-like language using the Plasma VM.
Let's call this language "C-ish".

The compiler must be capable of being *self* hosted.
It will be written to be a valid standard C program,
so it can be compiled by standard C
compilers, and be eventually capable of compiling itself.

The idea is that this compiler can take many *existing* C programs and
compile them into working code in many cases. It can optionally
output "optimization hints"; the code could then be annotated with
hints to optimize it further or silence that hint. The idea is that we
could take existing C code, compile it, fix up the optimization hints,
and get it faster. I want the hinting mechanisms (TBD) to silently
be ignored when compiled by a standard C compiler.

This means it must support char, int, unsigned (int), pointers and
addresses of variables, structs, unions. In the longer term I'd like to
support long, float, double, but those are later stretch goals.

Self-hosting means it must work with few resources.
I expect it to be implemented as a recursive decent parser.
There's not enough memory for AST-based systems, so it'd generate
code generally as it went along (streaming), with a peephole optimizer.
Streaming creates some challenges when parsing C, so it would need to
use some support structures such as small stacks to track information.
I currently have the following in mind:

1. Operator stack (expression engine). Store operator tokens/tags
   (e.g., +, ==, parens), say 16-32 entries. Deepens when parens
   are heavily nested.
2. Type modifier stack (declaration engine). Reverses C's
   "inside-out" declaration syntax, storing type modifier descriptions
   and payloads. Say 8 deep.
3. Control engine fixup stack. Tracks forward jumps for control flow
   such as if, else, while, for. Store integer buffer offsets.
   Say 32 entries.

These would be stored in global static arrays, acting as temporary
scratchpads during compilation.

Let's support at least <% and %>, as I think {...} were not
always easily available on Apple keyboards. 

I'm not sure how to deal with the Pascal strings that PLASMA prefers.
I'm also not sure how to deal with \n (maybe that should quietly emit
carriage return, as that was the end-of-line marker on Apple II).
I suspect we should generate constants as Pascal strings with \0 ending,
and return the address starting the data, for maximum compatibility.
It's not clear how to call the PLASMA routines that like Pascal strings,
but we need a way to do it.

## Design issues and options

This section lists the issues the approach raises, roughly in order of
how much they shape everything else. Each has options with pros and
cons, and a tentative recommendation (marked "Suggest") for discussion.
Settled items move to "Decisions so far" at the end.

Facts about the VM used below are from `../doc/Bytecodes.md`.

### 1. Target machine and memory budget

Self-hosting is the hardest constraint. A 64K Apple II leaves well
under 40K for the compiler's code, its tables, the runtime library and
I/O buffers together. On a 128K //e or //c, the module loader puts
bytecode in auxiliary memory, leaving "the main 40K for data"
(`doc/PLASMA.md`). The self-hosted PLASMA compiler (about 4,000 lines
of PLASMA in `src/toolsrc/*.pla`) is the existing proof that a compiler
of roughly this size can run there.

- **A. Self-host on 128K //e, //c and IIgs only; compiled programs run
  on any 64K Apple II.**
  - Pros: realistic, with the same split PLASMA already uses.
  - Cons: no self-hosting on a II+.
- **B. Self-host on 64K.**
  - Pros: runs on everything.
  - Cons: almost certainly forces a multi-pass design (see 2) and a
    much smaller language. It would also be slow.
- **C. Apple /// as a secondary self-host target.** It has more memory
  and PLASMA supports it. This could come later, alongside A.

Suggest A. Also set a size budget early, e.g. "compiler bytecode under
N KB, tables under M KB", and measure it on every commit.

Answer: Self-host on 64K. I think we can do this *without* multi-pass
through careful design, optimizations, and dropping "too hard"
functionality.

Consequences of the 64K decision:
- **Measure the free memory first.** On a 64K machine, bytecode, data,
  tables and ProDOS file buffers all share main memory below `$BF00`.
  Before writing much code, load a dummy module on 64K and record how
  much heap is actually free. That number becomes the budget, split
  between compiler bytecode, runtime library, tables and I/O buffers.
- **Track the budget on every commit**: compiler module size, plus the
  peak table use when the compiler compiles itself.
- **Keep tables compact.** Intern identifier strings once, keep symbol
  and type records small and fixed-size, and free per-function data
  (locals, labels) at the end of each function.
- **Check precedent.** Find out whether PLASMA's own self-hosted
  compiler runs on a 64K machine. If it does, its size and table usage
  are a useful reference.
- **Candidates to drop** if memory runs short. See open question 2.

### 2. One program, or several passes?

Classic CP/M C compilers (e.g. Hi-Tech C, Aztec C) ran as a chain of
programs: a preprocessor, a parser, a code generator and an optimizer,
passing temporary files between them. Each program fits in memory on
its own.

- **A. One program (lexer, parser and code generator), streaming.**
  - Pros: simplest; fastest on floppies; matches the plan above.
  - Cons: everything must fit at once, including symbol, type and
    struct tables.
- **B. A separate preprocessor program, then A.**
  - Pros: the macro table and the compiler's symbol tables never
    compete for memory. Under Linux we could use `gcc -E` until ours
    exists.
  - Cons: an extra temporary file and an extra pass over the source on
    the Apple II.
- **C. Phases as PLASMA modules run in turn.** PLASMA's
  `cmdsys:modexec()` loads a module, runs it, and frees it when it
  returns (see 18.4). It is a middle ground between A and B, but only
  helps if the phases don't need each other's data.

Suggest B: an integrated preprocessor would compete with the compiler
for exactly the memory that is scarcest.

Answer: Option A eventually, but let's not implement the preprocessor
to start with. Merged if we can, and support separate preprocessor later
if needed.

However, we'll implement preprocessor later. We need to do this in stages.

Resulting plan: one program. The first build has no preprocessor:
tests either avoid `#` lines or run through `gcc -E` on the host. The
minimal preprocessor (3B) is then built into the lexer. A separate
preprocessor program remains an option only for large programs, if
memory forces it. See "Milestones" below (M8).

**Revised (decided in discussion, 2026-10-29): the preprocessor will be
a separate program,** because memory now forces it: even without one,
the compiler had to be split into a compile pass and a final pass (18.6
B3). On the Apple II a build runs three programs, one at a time, each
loaded, run and freed by `modexec` from the `CCISH` driver:
1. **the preprocessor** (tentatively `ccpp`): `FOO.C` to `FOO.I`;
2. **the compile pass**, `cccomp`: `FOO.I` to the stream files;
3. **the final pass**, `ccrel`: the stream files to the REL module.

Why separate:
- **Memory.** The macro table, the `#if` stack and include handling
  never compete with the compile pass's tables (issue 2, option B), and
  the compile pass loses the preprocessor's code.
- **Open files.** PLASMA's Apple II file library allows only 4 open
  files (decision 18). Each nested `#include` holds a file open; in a
  separate program, the source, its includes and the one output file
  have all 4 to themselves, while the compile pass needs one input.
- **A simpler lexer** in the compile pass, and the same shape as on the
  host today, where `gcc -E` stands in for the preprocessor.

**The `.i` file is made small, to save disk space** (18.6 E3, F): the
preprocessor strips comments and unneeded spaces (indentation, runs of
blanks, blank lines), keeping only what separates tokens, and writes a
`#line` directive wherever its output would otherwise drift from the
source's line numbers (after a dropped run of lines, and on entering
or leaving an included file). So the compile pass reports errors at
the right file and line of the original source, which answers F2's
objection, and the `.i` file is about the "Neither" column of E3's
table (parse.c: 145K source, about 62K, plus the stripped headers).
Costs and limits, open for review:
- An extra pass over the source, and an extra temporary file on disk
  while the preprocessor runs (the source and the `.i` at once); then
  the compile pass needs only the `.i` and the streams. parse.c's
  source alone still doesn't fit a floppy, so F remains open for the
  source itself.
- The compile pass must understand `#line` (and, on the host while
  `gcc -E` stands in, gcc's `# N "file"` markers).
- With 4 open files, includes can nest at most 2 deep (source, include,
  nested include, `.i`), unless the preprocessor closes and reopens a
  file to free its slot.

### 3. Preprocessor

The approach doesn't mention it, but existing C programs (and the
compiler itself) need `#include`, `#define`, `#if`/`#ifdef`, and
usually function-like macros. The hint mechanism (issue 10) may also
depend on `#ifdef`.

- **A. None at first: run `gcc -E` on the host.**
  - Pros: zero work now.
  - Cons: blocks self-hosting and real use on the Apple II.
- **B. Minimal:** `#include`, object-like `#define`, `#if`/`#ifdef`/
  `#else`/`#endif`, with `#if` limited to `defined()` and integer
  constants.
  - Pros: covers most of our own code.
  - Cons: many existing programs use function-like macros.
- **C. Full C89 preprocessor:** function-like macros, `#` and `##`, and
  full `#if` expressions.
  - Pros: compiles existing code unchanged.
  - Cons: sizeable. Macro bodies need memory.

Suggest A, then B, then C, as a separate program (issue 2). Write the
compiler's own source so it needs only B.

Answer: B. Revised 2026-10-29: as a separate program that writes a
compact `.i` file (see issue 2, "Revised").

### 4. Streaming code generation for a stack machine

This is the core technical issue. Most of C maps naturally onto
PLASMA's stack code: once you reach a binary operator, the left operand
is already on the stack, and you know the result type by the time you
emit the operator. Three cases do not fit:

**(a) Stores take the address on top of the stack.** `SB`/`SW` are
`( val addr -- )`, and there is no SWAP. For `*p = e` or `a[i] = e`, a
streaming parser sees the address first but must emit it after the
value. (SWAP did exist early on, as opcode `$2E`. Commit `3f48559d`,
"Add in-line strings", 2015, reused that opcode for `CS`. Adding it
back would need a VM change on every platform, so we assume it is
gone.) PLASMA's own compiler deals with this by building each lvalue's
code as a separate sequence in memory and appending it after the
right-hand side (`setseq` in `src/toolsrc/parse.c`). That is option C
below.
  - **A. Deferred lvalues** (the approach Small-C used). For
    `variable`, `*variable`, `variable[constant]` and `var->field`,
    don't emit code at all. Keep a small descriptor, and emit the
    address and store after the right-hand side. Local and global
    scalars need no address anyway (`SLW`/`SAW`).
    - Pros: optimal code for the common cases.
    - Cons: only covers simple lvalues.
  - **B. Spill the address to a temporary local:**
    `addr; SLW t; e; LLW t; SW`.
    - Pros: always works.
    - Cons: 4 extra bytes and slower.
  - **C. Code motion.** Note where the address code started, emit `e`,
    then move the address code after it. PLASMA branches are relative,
    so a moved block's internal branches stay valid. Only the fixup
    offsets inside the moved code need adjusting.
    - Pros: optimal code for any lvalue.
    - Cons: memmove within the code buffer, plus fixup bookkeeping.

  Suggest A, with C (or B) for complex lvalues.

**(b) The left operand sometimes needs conversion that you can't
decide until you see the right operand.** In `i + p`, `i` must be
scaled by the size of `*p`, but `i` is already emitted and is now under
`p`. Likewise, `i < u` (with `u` unsigned) needs an unsigned compare,
and later `long` will need widening. The options are the same as in
(a): insert code after the left operand (C), or spill it (B). These
cases are rare, so either is fine.

**(c) `for (init; cond; incr) body`:** `incr` comes before `body` in
the source but runs after it.
  - **A. Jump around it**, as Small-C did:
    `cond; BRFLS end; BRNCH body; incr: …; BRNCH cond; body: …; BRNCH incr`.
    - Pros: trivial.
    - Cons: two extra branches per loop iteration.
  - **B. Emit `incr` and move it after `body`.**
    - Pros: optimal, and it also enables PLASMA's fused FOR branches
      (`INCBRLE` etc.).
    - Cons: code motion, as in (a)C.
  - **C. Remember `incr`'s source position and re-parse it after the
    body.**
    - Pros: no code motion.
    - Cons: the lexer must be able to seek back, and errors in `incr`
      are reported late.

  Suggest B, since it reuses the same code-motion mechanism as (a)C and
  (b). If we build that mechanism once, the three problems share it.

**(d) Unifying approach: pseudo-ops resolved by a delay buffer**
(proposed in discussion, and now the suggested approach for (a)–(c)).
The code generator doesn't write instructions straight to the output.
It appends records (opcode, operand, flags) to an expression-sized
delay buffer, which is also the peephole optimizer's window. It can
also emit pseudo-ops that exist only in that buffer:
- `MARK`: a block boundary.
- `SWAP2`: exchange the two preceding blocks.
- `SCALE n`: scale the block that starts at a marker by `n`.
- `UFLIP`: XOR the block at a marker with `$8000`, for unsigned
  compares.

The optimizer resolves these before flushing, so no SWAP runs at run
time. `*p = e` is emitted as `MARK p MARK e SWAP2 SW` and comes out as
`e p SW`.

- **Why this is legal C.** The order of evaluation of the operands of
  `=` and of most binary operators is unspecified. Only `&&`, `||`,
  `?:`, `,` and function calls impose an order, so exchanging blocks is
  allowed.
- **Compound assignment** (`*p += e`) needs the address both before and
  after `e`:
  - If the address block is pure and cheap (`p`, `a[i]`, `s->f`), the
    optimizer emits it twice.
  - Otherwise (`*p++ += e`), it spills the address to a temporary local
    (`SLW t` … `LLW t`).

  This is also the fallback whenever a block is too big to hold.
- **Branches and relocations** stay symbolic (label numbers, "needs
  relocation" flags) while in the buffer. They are resolved on flush,
  so moving a block needs no patching.
- **Size.** About 128 records of 4 bytes each (≈512 bytes). The buffer
  flushes at the end of each full expression. Labels are barriers for
  peephole patterns, but not for block moves inside an expression.
- **`for` increments** can't use this, because the loop body is
  unbounded. The increment's records go to a small side buffer, which
  is flushed after the body. That also enables the fused FOR opcodes.
- **Alternative considered: divert code into a side region.** Code
  for a possible assignment target would go to another buffer and be
  appended after the right-hand side. Branch patching is not the
  obstacle:
  - Expression branches (`&&`, `||`, `?:`) stay within their
    subexpression and are relative, so a copied block still works.
  - Pending statement-level patches stay valid as long as the main
    buffer is only appended to.

  The obstacle is that C reveals an lvalue only after its code exists:
  `*p` followed by `=` versus `*p + 1`. Every possible lvalue would
  have to be diverted speculatively and usually copied back. On top of
  that, absolute fixups need rebasing and nested targets
  (`a[b[i] = x] = y`) need a stack of regions. That amounts to the
  delay buffer, done less simply. Either way, the key requirement is
  that code stays movable (unflushed, branches relative, fixups
  symbolic) until the end of the full expression.
- **Adding a real SWAP opcode** isn't needed. The rare fallback is a
  temporary local, and a new opcode would break compatibility with
  existing PLASMA VMs. (Since reopened as a possibility: issue 21.)

**(d2) Rotation in place, with marks held by the parser (decided in
discussion, 2026-10-29; M5 step 4).** This is a simpler way to build
(d)'s block exchange, and it is moved up from M10.
- **Order problems versus reuse problems.** The spills ccish has fall
  into two kinds:
  - *Order:* the code is right but in the wrong order. A store to a
    computed address (`*d++ = *s++`, `a[i + 1] = x`, `*f() = x`)
    emits the address before the value, and `i + p` emits `i` before
    it knows `i` must be scaled. Exchanging two blocks of code fixes
    these, with no temporary.
  - *Reuse:* a value is needed twice. Compound assignment and `++`/`--`
    on a computed address (`a[i] += e`, `*p++ += e`, `(*f())++`) need
    the address for the load and again for the store. No reordering
    can make a second copy of it, so these keep their spill. That
    takes DUP plus SWAP, or OVER, which the VM doesn't have (issue 21).
    The spill is already the cheapest option (4 bytes, 2 frame bytes),
    and recomputing a pure address is larger and slower (M4 step 3
    notes).
- **The mechanism.** Stage 2 (`ir_put()` in `lower.c`) holds the IR
  records of the current expression in a buffer before lowering them.
  The parser takes marks (buffer indexes) in its own local variables.
  No `MARK` or `SWAP2` ops go into the stream:
  ```
  m0 = ir_mark();  parse the lvalue;  m1 = ir_mark();  parse e;
  if (ir_rotate(m0, m1)) emit the store;  else fall back;
  ```
  `ir_rotate(m0, m1)` turns `[m0, m1) [m1, end)` into
  `[m1, end) [m0, m1)` in place, with the three-reversal method
  (reverse each block, then reverse both together; Bentley,
  *Programming Pearls*, column 2). It needs no extra memory, and each
  block keeps its internal order.
- **Why moving IR records is safe.** IR ops lower one at a time, with
  no state carried between them, and branches name labels by number,
  so nothing inside a moved block needs patching. The peephole stage
  sees the final order, so its existing patterns (such as
  `x LLA a LLW i IDXW SW`) apply as they are.
- **Nesting.** A rotation keeps the length of its range, so marks
  taken before it stay valid, and inner rotations finish before outer
  ones (`a[b[i] = x] = y`).
- **Flushing.** The buffer passes records on to lowering whenever no
  mark is open, and always at the end of a full expression
  (`end_expr()`). Rotation never crosses a statement.
- **Overflow.** If the buffer fills while a mark is open, it is flushed
  anyway, and the open marks become stale (a generation count), so
  `ir_rotate()` returns 0. The parser then swaps after the fact with
  two temporaries (`SLW t1 DROP SLW t2 DROP LLW t1 LLW t2`): large,
  but rare and always correct. The buffer holds about 64 records of
  about 5 bytes each (op, operand, symbol pointer), so about 320 bytes.
- **Details.** `put()` writes held constants (folding) into the buffer
  first, so a mark is taken only after they are written. Code skipped
  by `suppress` writes nothing, so marks there are harmless.
- **A side benefit:** `*p op= e` can evaluate `e` first
  (`e LLW p LW ADD LLW p SW`). Both reads of `p` then follow `e`,
  which fixes the mismatch with C noted in the M4 step 3 notes. (As
  built, this needs no rotation, and covers every operator: see the
  "M5 step 4 is done" notes.)
- **Not covered:** the `for` increment, since the loop body has no size
  limit. It still needs (d)'s side buffer, in M10.
- **Related techniques.** There is no single standard name for this.
  Small-C's staging buffer (`setstage`/`clearstage`) lets a one-pass
  compiler rewrite recent output before it is final. Wirth's "items"
  (*Compiler Construction*) and TCC's value stack delay simple operands
  instead, which ccish's pending operands (`K_VAR`, `K_ELEM`) already
  do; rotation handles the rest. Assemblers and JITs use side streams
  (subsections, "deferred code") for code that must come later.

**(e) Is avoiding SWAP worth the effort?** (This answers a question
from the discussion.) Yes, mainly because it costs much less than it
first appears:

- **Spilling the top of stack to a temporary local is a universal
  substitute for SWAP, even after the fact.** With `a b` on the stack:
  `SLW t; …work on a…; LLW t`. For example:
  - `i + p` becomes `i p SLW t CB 2 MUL LLW t ADD`.
  - An unsigned compare becomes
    `a b SLW t CW $8000 XOR LLW t CW $8000 XOR ISLT`.
  - `*p = e` becomes `p SLW t e LLW t SW`.

  None of these needs a buffer. A purely streaming first version can
  therefore be correct with no SWAP and no block moves.
- **The block-exchange is then only an optimization.** Stage 2 of the
  pipeline (4A) has to hold an expression anyway, to choose opcodes
  such as `DLW` and `ADDLW`. Exchanging two blocks of records there is a
  small addition, and it removes the spills in the common cases.
- **SWAP would cost more overall.** It needs:
  - changes to every VM (6502, 128K, both JITs, 65802, Apple ///, C64,
    and the C VM);
  - a new opcode slot;
  - accepting that modules no longer run on existing PLASMA installs.

  It would still cost 1 byte and a dispatch on every `*p = e`. Block
  exchange costs nothing at run time.

So build it in stages. Early milestones use spills everywhere, plus
the jump-around `for` (4(c)A). M10 adds the block exchange and the `for`
side buffer.
Measure before considering SWAP again. (Update, 2026-10-29: the block
exchange moved up to M5 step 4, built as rotation in place; see
(d2). The `for` side buffer stays in M10.)

Other streaming items:
- Function frame size isn't known until the function ends, so `ENTER`
  gets patched.
- `switch` cases are collected on a case stack, and the `SEL` table is
  emitted after the body. This needs a fourth fixed-size stack for
  case values (e.g. 64–128 entries). Fall back to a compare chain if
  the case values span more than 32767 (a 6502 VM limit).

### 4A. Architecture: a pipeline of op streams (adopted)

Proposed in discussion and adopted; the IR op spec comes after the
other open questions are settled. Instead of building an AST, the compiler pipes
a stream of ops through a few small stages:

1. **Parser → intermediate ops (IR).** Typed and symbolic: `PUSHVAR x`,
   `PUSHCONST 5`, `BINOP + int,ptr(int)`, `STORE char`, `LABEL n`,
   `JMPF n`, `CALL f,nargs`, `FUNC_BEGIN`/`FUNC_END`, plus the
   pseudo-ops of 4(d) (`MARK`, `SWAP2`, …). The parser never chooses
   opcode encodings.
2. **Lowering.** Holds one full expression (the delay buffer of 4(d)).
   It resolves the pseudo-ops and chooses real opcodes
   (`CN`/`CB`/`CW`, `DLW`, `SLW` vs. `SW`, `ADDLW`, …).
3. **Peephole** over real ops, with a small window.
4. **Emitter** (`emit.c`). Turns labels into relative offsets and fixups into
   RLD entries, and writes the module.

Precedents:
- Davidson and Fraser's retargetable peephole optimizer (around 1980):
  naive expansion followed by pattern combining. GCC's RTL descends
  from it.
- Conway's 1963 coroutine-structured compiler, the origin of the term
  "coroutine".

Pros:
- Each stage is small and can be tested alone; the IR can be dumped as
  text and diffed.
- Only stage 2 maps types to opcodes. A future native 6502 back end
  could replace stages 2–4 and keep the front end.
- Memory is bounded per stage:
  - stage 2: about one expression;
  - stage 3: a few ops;
  - stage 4: the labels and pending patches of one function.
- **Multi-pass for free.** If memory gets tight, cut the pipe between
  stages 1 and 2 and write the IR to a file. That answers issue 2
  without committing now.

Cons:
- **Compile speed.** The compiler itself runs interpreted, and every op
  crosses each stage boundary. Making each stage a push-style function
  (`stage2_put(op)`), not a separate pass, keeps that to about one call
  per op per stage.
- **An IR spec is needed.** That's design work, but it's also the
  natural next document.
- **The postfix IR stream is a flattened expression tree.** Stage 2 is
  in effect a per-expression tree. That's acceptable: the memory
  concern was about whole-function or whole-program ASTs, and this
  stays at a few hundred bytes.
- **Some things still need holding or patching outside any window:**
  the `ENTER` frame size, `switch` tables, `for` increments, and
  forward branches in stage 4.

This architecture absorbs 4(d) (into stage 2) and issue 13 (into
stage 3).

### 5. Recursion depth (in the compiler and in compiled programs)

On the 6502 VMs, each bytecode call uses about 4–6 bytes of the
256-byte hardware stack, which allows roughly 40–60 levels of
recursion. A textbook recursive-descent C parser has one function per
precedence level (about 15). With that, `((a))` alone would nest 30+
calls deep and overflow.

- **A. Precedence climbing:** one function and a loop for all binary
  operators.
  - Pros: about 4–5 calls per parenthesis level.
  - Cons: none really. It is a standard technique.
- **B. Shunting-yard with an explicit operator stack** (your item 1).
  - Pros: depth is bounded by the table, not the hardware stack.
  - Cons: unary, postfix and call syntax become fiddlier.
- **C. Make statement parsing iterative where it's cheap**, e.g.
  `else if` chains and blocks.

Suggest A for expressions, which still needs a small operator stack in
practice, plus a nesting limit that gives a clear error instead of a
crash. Compiled C programs face the same limit; document it.

Answer: precedence climbing (option A) is fine.

### 6. Calling convention and frames

- **Varargs.** `printf` is everywhere. The PLASMA callee's `ENTER`
  takes a fixed number of parameters off the evaluation stack.
  - **A.** The caller stores the extra arguments into a block in its
    own frame and passes its address plus a count.
    - Pros: works with the existing VM. `stdarg.h` becomes simple
      pointer walking.
    - Cons: the caller must know the function is variadic, which a
      prototype tells it.
  - **B.** Treat `printf` and friends as compiler builtins.
    - Cons: not general.

  Suggest A.

Answer: Agree, A.

  Decided in discussion: **a variadic function must be declared with a
  prototype ending in `...` before it is called**, for example in a
  `.h` file. C89 already requires this (calling a variadic function
  without a prototype is undefined behavior), and it's what tells the
  caller to build the argument block.

  (Varargs is not in the first milestone, so early test programs and
  the runtime use fixed-argument output functions instead of
  `printf`.)

- **Frames are at most 255 bytes.** `char line[256];` as a local
  doesn't fit.
  - **A. Allocate large locals from the heap or a separate C stack**,
    released on return.
    - Pros: keeps C semantics, including recursion.
    - Cons: an extra prologue and epilogue for those functions.
  - **B. Make large locals static.**
    - Cons: breaks recursion and reentrancy. It could be a hint (see
      10) rather than a default.
- **The evaluation stack is 32 entries**, shared by expression depth
  and pending call arguments. Report an error if the compiler can see
  it would overflow.
- **Struct values (pass, return, assign) and `long` returns** need a
  hidden pointer, because PLASMA functions return exactly one word.
  (Decided in M1: as in PLASMA's `#0`, a `void` function returns no
  value at all. That saves a `CN 0` and a `DROP` per call, and using a
  void value is a compile error.)

### 7. Integer types

- `int` = `short` = 16 bits, `long` = 32 bits (later), and pointers are
  16 bits. All of this is legal C.
- Plain `char` is unsigned, because `LB` zero-extends. `signed char`
  needs sign extension (the CMDSYS `SEXT` routine, or inline code).
- `unsigned` needs special code for `< <= > >=`, `/`, `%` and `>>`,
  because PLASMA's are signed (`SHR` is arithmetic).
  - Compares: XOR both operands with `$8000`, then do a signed
    compare. This works inline, subject to the left-operand problem in
    4(b).
  - `>>`: shift, then mask.
  - `/` and `%`: a runtime routine.
- Avoid `DIVMOD` and `NATV`, and never use a `NOT`'s raw value, because
  of the differences between the C and 6502 VMs (see
  `../doc/Bytecodes.md`). `NOT ANDI 1` is safe (issue 17).

### 8. Strings and calling PLASMA routines

Decided: each literal is stored as a length byte, then the characters,
then a NUL (`[len][chars][NUL]`). Its C value points at the first
character, so `s - 1` is the PLASMA string. This is what your last
paragraph suggests.

Remaining choices:
- **How C code gets the PLASMA form:**
  - **A.** A builtin `pstr("lit")`. The compiler checks the literal is
    255 characters or fewer.
    - Pros: explicit and checked.
    - Cons: not standard C, so it is only for code that runs on
      PLASMA.
  - **B.** Write `"lit" - 1` by hand.
    - Pros: standard.
    - Cons: unchecked and cryptic.
  - **C.** Hide it behind runtime wrappers that take C strings and
    convert internally, e.g. a `c2pstr` copy.
    - Pros: callers just write C.
    - Cons: a copy on every call.

  Suggest A inside the runtime library, and C for anything user-facing.
- **Arrays initialized from literals** (`char b[] = "hi"`) don't get the
  hidden length byte, because they can be modified.

### 9. Newlines (`\n` vs. Apple II carriage return)

- **A. `'\n'` compiles to 13 (CR).**
  - Pros: no translation; Apple II text files and the screen just work.
  - Cons: code that expects `'\n' == 10` breaks, e.g. when reading
    files that came from Linux.
- **B. `'\n'` stays 10, and stdio translates 10↔13 in text mode**, as
  DOS C compilers did with CR/LF.
  - Pros: standard behaviour; binary files are unaffected.
  - Cons: a little per-character work in the runtime.

Suggest B. Also make the lexer accept CR, LF and CRLF line endings, so
source files from either world work.

### 10. Optimization hints

These must be silently ignored by other compilers.

- **A. Special comments**, e.g. `/*@ nonrecursive */` (the lint/splint
  tradition).
  - Pros: every C compiler ignores them, with no warnings.
  - Cons: the lexer must parse comments, and hints can't be produced
    by macros.
- **B. `#pragma ccish ...`.**
  - Pros: standard says unknown pragmas are ignored.
  - Cons: `gcc -Wall` warns about unknown pragmas. Pragmas can't go
    inside expressions (`_Pragma` is C99).
- **C. Macros from a header**, e.g. `CCISH_NONRECURSIVE`, defined as
  empty unless `__CCISH__` is defined.
  - Pros: clean and flexible.
  - Cons: needs the preprocessor (issue 3), and every source file must
    include the header.

Candidate hints, to check against what actually speeds up PLASMA code:
- A function is non-recursive, so its locals can be static and use
  absolute addressing.
- A value is never negative, so it can use cheaper unsigned or byte
  operations.
- A loop fits PLASMA's FOR-branch opcodes.
- A switch should be a table.

Hints the compiler could emit are mostly the reverse: "this local
array went to the heap", "this compare needed an unsigned fixup".

Suggest C, with A as a fallback for code that can't include a header.
Defer the details until code generation works.

### 11. Separate compilation, linking and names

Each C file can become one PLASMA REL module. PLASMA resolves imports
by name at load time, but each module also lists the modules it
depends on by name. The C source doesn't say which file defines an
`extern` function.

- **A. Single translation unit** (`#include` all the `.c` files, or
  concatenate them).
  - Pros: nothing new to build.
  - Cons: one large module; the compiler's own source must fit in one
    compile.
- **B. Declare dependencies in the source**, e.g. a hint or pragma
  naming the module.
- **C. A small linker** that merges REL files into one module.
  - Pros: the classic model.
  - Cons: another program to write.

  *Update (issue 18.1):* the planned final pass already combines a
  compiled file's streams (data, code with relocations, symbol summary)
  into a REL module. Given the streams of **several** compiled files,
  the same pass is that linker. It would:
  - concatenate the data streams and the code streams;
  - offset each file's relocations;
  - resolve calls between the files internally, so only real imports
    remain.

  So option C costs little extra once 18.1 exists, and the stream
  format becomes our object file format. Cross-file references to
  globals would come with globals (M4).

Also:
- PLASMA symbols are case-insensitive (stored uppercase), but C is
  case-sensitive, so `foo` and `Foo` would collide. Exported names also
  keep only their first 16 characters (`doc/PLASMA.md`). Options:
  reject collisions among exported names, or mangle them. This only
  affects names exported or imported across modules; names private to
  one module never reach the symbol table.
- The REL format has no BSS segment. Large uninitialized arrays bloat
  the file unless the init function allocates them. (Decided for M4:
  zero bytes in the data segment for now; see "M4 design" under
  Milestones.)

Suggest A first, then B.

**Decided in discussion (2026-10-30): C, a static linker in `ccrel`**
(decision 20). ccish's own source needs separately compiled files, and
shares data as well as functions between them (`ccish.h` declares
`extern int tok;` and `extern int fn_name[MAX_FUNCS];`).

- **Declarations are required.** Calling a function with no
  declaration or definition in scope is an error, not a C89 implicit
  declaration (returning `int`, with its parameter count fixed by the
  first call). Without a declaration, a wrong argument count or return
  type would be caught only at link time, if at all. C99 dropped
  implicit declarations too. The CMDSYS table (`cmdsys_funcs[]`)
  carries full signatures, so its names stay declared as built-ins
  until M8 can supply a `cmdsys.h`. This is C3's "implicit function
  declarations" item, though it saves little compiler code:
  `pool_release()` must still keep the names of CMDSYS functions first
  called inside a function. A declaration with `()` is still allowed
  (C89), and the first call then fixes its parameter count.
  **Done** (2026-10-30): message 215, "function called without a
  declaration".
- **Static linking.** `ccrel` links several objects into **one** REL
  module, which exports nothing and imports only CMDSYS functions, as
  it does now. So C names never reach PLASMA's symbol table, and the
  case and 16-character collisions above don't arise: `ccrel` matches
  names in full, with their case. (One REL module per `.c`, importing
  from each other, would expose them; rejected.)
- **Object format 4** (OBJECT.md):
  - `.syms` describes data symbols too: a "data" flag, and for a
    definition its data offset and size.
  - A "local" flag for `static` functions and globals: they stay out
    of the global namespace, so two files may each have a `static`
    helper of the same name.
  - A new relocation kind, a symbol's address plus an offset, for
    another file's data (`tok`, `&tbl[3]`). A function address
    (kind 2) already names a symbol.
  - A 16-bit hash of each function's full type, so that `ccrel` can
    check calls between files beyond the parameter count and return
    flag `.syms` already has (`char *` against `int`, say). Two bytes a
    symbol; we may drop it if space demands.
- **The compiler's side.** A function declared but not defined is an
  undefined symbol, not an error; `ccrel` resolves it to another
  object's definition, or a CMDSYS import (the compiler flags those
  from its table), or reports "undefined reference". `extern int x;`
  declares a data symbol without allocating it. No common symbols:
  `int x;` in two files is a duplicate definition (C11 6.9p5 allows
  only one external definition), which ccish's own source never needs.
- **`ccrel`'s passes**, with one input file open at a time and the
  module:
  1. Read every object's `.syms`: lay out each object's data, strings
     and code; build one table of the global (non-`static`) names
     (about 300 for ccish itself, roughly 4-5K); report duplicates,
     undefined names, and mismatched counts, return flags or type
     hashes.
  2. Write the data, then the strings, then the code, object by
     object. Each object's `.syms` is read again to map its own symbol
     indexes to the global ones, so only one object's map is in memory.
  3. Write the RLD by reading the objects once more and computing the
     entries from their `R` and `F` records (18.6 B4), so relocations
     aren't held in memory: linking the whole compiler needs perhaps
     10K of them. (A temporary RLD file would need a third open file;
     within PLASMA's 4, but tighter.)
- **Relinkable output.** If reasonable, `ccrel` should also be able to
  write an object instead of a module (like `ld -r`): several small
  objects linked into one larger one, which can be linked again. The
  output is the same format: data, `R`, `S` and `F` records in order,
  and a merged `.syms` with local symbols kept local and undefined
  ones left undefined.
- **Libraries, later** (consistent with the design, not built yet): an
  archive, like Unix `.a`, of objects plus an index of the names each
  defines, from which `ccrel` takes only the members something refers
  to. So `ccrel` handles each object through "open object *k*'s
  summary" and "open object *k*'s stream", which an archive member can
  provide too (with PLASMA's set-mark to find it). Until then, all
  objects are named on the command line.
- **Order of work:** (1) require declarations, its own commit;
  (2) object format 4 and the compiler's side; (3) link several
  objects in `ccrel`, with relocations still held in memory, enough
  for the tests; (4) write the RLD by rereading the objects (B4), so
  the whole compiler can be linked.

**Object format 4 and `extern`, in detail (decided in discussion,
2026-10-30).** OBJECT.md has the exact layout.
- **`.syms`** gains a data symbol count after the function count.
  Function entries gain flags 8 (local: `static`) and 16 (a CMDSYS
  function, declared from the table or matching it), and a type hash
  word. Data symbols follow the functions: flags (1 defined, 2 used,
  8 local), data offset if defined, size (0 if unknown, as in
  `extern int a[];`), type hash, name. Data symbols are numbered by the
  compiler's global variable index (globals are never popped, so the
  index is fixed at the first reference), and every global is listed;
  static locals aren't. "Used" lets `ccrel` ignore an `extern` that a
  header declares and the file never uses.
- **Relocation kind 4, a data symbol plus an addend,** for a reference
  to a global declared `extern` and not (yet) defined in this file.
  In an `F` record it is the usual 5 bytes, and the addend is the code
  word itself (written as 0 for the other kinds), as in classic
  relocatable formats. In the data, an `R` record of kind 4 has the
  symbol and then the addend (5 bytes, not 3). A reference to a global
  with storage in this file stays kind 1; one made before the
  definition is kind 4, resolved against the object's own symbol.
- **Type hash:** `h = (h * 31 + byte) & 0xFFFF` over a canonical
  string: `i` int (and enums), `c` char, `v` void, `s<tag name>;` a
  struct (by name: tag numbers differ between files), `k` const and
  `V` volatile before what they qualify, `*` then the target, `[`,
  the count (2 bytes), then the element; a function is its return
  type, `(`, its parameters (their own qualifiers dropped), `)`. A data
  symbol's outermost array count is left out, so `extern int a[];`
  matches `int a[10];`; the size field is checked instead, when both
  know it. A function declared with `()` hashes to 0, "unknown": only
  its return flag is checked. (A hash that comes out 0 becomes 1.) The
  hash is the same on the host and in ccish: `h` is below 2^16 before
  the multiply, so a 32-bit `int` never overflows.
- **Compiler:** `extern` at file scope declares a global without
  storage, or (on a function) changes nothing. `extern` with an
  initializer, and `extern` in a block, are errors (legal C, rare). A
  later definition gives an `extern` global its storage. Linkage
  follows C11 6.2.2: `static` after a non-`static` declaration is an
  error, and `extern` or no specifier after `static` keeps it static.
  Globals get `var_link[]` (static, used, no storage yet), 72 bytes,
  since `var_flags[]` is full; functions get `FN_STATIC`. A global
  operand's `global` field becomes three-way (frame, this file's data,
  or extern variable *v*), and the global IR ops carry *v* in their
  function operand, lowered to `FIX_DSYM`.
- **Undefined names move to `ccrel`:** a function called but not
  defined, other than a CMDSYS one, is an undefined symbol, and
  `ccrel` reports "undefined reference" (message 172, reworded), as
  for a used, undefined global. A declaration of a CMDSYS name that
  doesn't match CMDSYS isn't an error in the compiler any more (another
  file may define that function); the compiler flags it (32), and
  `ccrel` reports message 173 only if nothing defines it.

**Step 2 is done** (2026-10-30; object format 4, 237 tests). Every
earlier run test's module and listing is byte for byte what format 3
gave. Choices made while building it, open for review:
- The parser's `Operand.global` is 0 (frame), 1 (this file's data) or
  2 + *v* (extern global *v*, `var_where()`); `load_var()` and the
  other helpers pass *v* as the IR op's function operand. The
  peephole rule `SAW a LAW a` → `DAW a` now also compares that operand,
  since two extern globals both have addend 0.
- A global is "used" only by code or data actually emitted, so
  `sizeof ext` doesn't make it need a definition.
- `sizeof` an array of unknown size (`extern int t[];`) is now error
  79, which before could only arise in a parameter, where it is a
  pointer. Before this, it would have given 0.
- Redeclaring a global with storage is still "redeclaration" (27),
  checked before the types (165), so `int x; char x;` reports as it did.
- New messages: 216 (`extern` with an initializer), 217 (`static`
  after a non-`static` declaration, for globals and functions), 218
  (no type after `extern`), 219 (`extern` in a block or a `for`). 172
  is reworded "undefined reference to", and 172 and 173 now come from
  `ccrel`, after the source is closed, so they name no line.
- A `static` function named `main` isn't the module's entry point.
- Type hashes are computed when the summary is written, from the
  types then known. A struct tag declared only in a parameter list may
  have had its name released by then (`pool_release()`), so its hash
  is unreliable; such a function can't be called with a compatible
  argument anyway.
- `ccrel` (still one object) reads data symbols into `dsym_off[]`
  (`MAX_VARS` ints), reports a used, undefined global while reading the
  summary, and skips the hashes. A kind 4 relocation is kept as the
  kind 1 it resolves to, so the listing shows `data+`*n*, not the name.
- Tests: `run/extern.c` (extern globals used before their definitions,
  arrays without a size, addresses in initializers, an unused extern,
  `static` and `extern` functions), `run/extern_ops.c` (struct copies,
  compound assignment and `++` through extern globals), and error tests
  for messages 79, 165, 172 (a global and a static function), 216, 217
  (a global and a function), 218 and 219.

**Step 3, linking several objects, in detail (decided in discussion,
2026-10-30).**
- **Command line:** `ccrel [-l] [-o module] output...`, one or more
  objects (base names as given to `cccomp`), linked in that order; `-o`
  is required with more than one. `ccrel` no longer removes its inputs
  (a linker that deletes its objects defeats separate compilation), so
  `-k` moves to `ccish`, which removes the objects it made itself.
  `ccish` still compiles one file; several files go through `cccomp`
  and `ccrel`.
- **Layout:** every object's data, then every object's string pool,
  the pad byte, every object's code, then init. With one object, that
  is the layout of before, so every run test's module and listing
  stays byte for byte the same.
- **Pass 1** reads each `.syms` in turn: it records each object's data,
  string and code bases, and puts each non-local symbol in one global
  table (name, function or data, flags, parameter count, hash, size,
  defining object and offset), with its own limits, not `MAX_FUNCS` or
  `MAX_VARS`, and a linear search for now.
  - A second definition of a non-local name (two functions, two
    globals, a function and a global, two `main`s) is message 220,
    "multiple definition of", as `'x' (in a and b)`.
  - Each entry keeps the most complete description met so far (a known
    hash replaces 0, a known count replaces 255); each later
    declaration or definition is compared with it: parameter count,
    return flag, hash when both are nonzero, and for data the size when
    both are nonzero. Comparing everything with one kept description
    finds any disagreement between two files. Unused declarations are
    compared too. A mismatch is message 221, "conflicting types for",
    as `'x' (in a and b)`.
  - After all the objects: a name used and defined somewhere links to
    that definition. A program's own definition wins over CMDSYS (it
    may supply its own `puts`, if the types agree). A name defined
    nowhere is a CMDSYS import if every reference has flag 16, else
    173 (flag 32) or 172. A local symbol never enters the table; one
    used and not defined is 172 when its object is read. The entry
    point is the global `main`; imports are numbered across all the
    objects. As in the compiler, the first error stops.
- **Pass 2** is as before, object by object. For the data and again
  for the code, each object's `.syms` is reread to map its function
  symbols to a final code address or import, and its data symbols to a
  final data offset; then its records are copied, with kind 1 and 3
  targets offset by the object's bases and kind 4 resolved through the
  map. Only one object's map is in memory. Relocations stayed in
  memory until B4 (step 4), now done: see "Step 4 (B4) is done".
- **Listing:** `data+`*n* and `string+`*n* are module offsets; function
  names come from the object being copied. With several objects, each
  object's code starts with an `object `*name*`:` line.
- **Relinkable output (`-r`), step 3b, a commit of its own:** the same
  records in the same order, kind 1 and 3 targets rebased, kind 4 kept
  with the merged data symbol number, and a merged `.syms` with one
  entry per global name and each object's local symbols kept as local
  entries of their own. Undefined names stay undefined; duplicates and
  conflicting types are still checked. No init, no RLD.
- **Tests:** `tests/link/NAME/` holds `.c` files compiled and linked in
  name order, and `expected` (the run output) or `expect-error` (the
  message text). No `#include` yet, so each file writes its own
  declarations, which also makes mismatches easy to test.

**Step 3 is done** (2026-10-30; 255 tests). Every earlier run test's
module and listing is byte for byte what step 2 gave. Choices made
while building it, open for review:
- `link_module()` takes the objects' output names; `ccish` passes its
  one. `ccrel` has no `-k`, and stops with its usage line for several
  objects without `-o`.
- The limits are `MAX_OBJS` 32, `MAX_GSYMS` 512 and `MAX_GNAMES` 6144,
  in `link.c` (not `ccish.h`, since only the linker uses them). A
  shared name's entry (`Gsym`, functions and globals alike) is 20
  bytes with a 16-bit `int`, so the table and its names take 16K: fine
  on the host, but to be sized down for the Apple II with B4 (ccish
  itself has about 300 shared names). Each object must still fit
  `MAX_FUNCS` and `MAX_VARS`, which step 3b's merged objects may not:
  they will need limits of their own.
- `lsyms[]` now holds only the current object's function symbols
  (names for the listing); the maps are `fmap[]` (code offset, or
  -2 - import number) and `dmap[]` (data offset), rebuilt for each
  object in the data pass and again in the code pass.
- Messages 220 and 221 name both objects, `'x' (in a and b)`: for 221,
  the object of the description kept, which may not be the first to
  declare it. 222 and 223 are the new limits. Messages from `ccrel`
  still begin `ccish:`, as all three programs' do.
- Undefined names are checked in table order (the first object's
  functions, then its globals, then the next object's new names), and
  only the first is reported, as everywhere.
- The damaged-file message now names the object's stream file
  (`NAME.obj`), not just the suffix.
- Tests: `tests/link/basic` (functions and globals both ways,
  addresses in another object's array in data and code, same-named
  `static` functions and globals) and `main_second` (main in the
  second object, a program's own `toupper`, a `()` declaration), and
  14 error directories: 220 for two functions, two globals, two
  `main`s, and a function and a global; 221 for a function against a
  global, parameter count, return, type hash, size, and an unused
  declaration; 172 for a function, a global, and another object's
  `static`; 173. Also `ccrel` without `-o` for two objects.

**Fitting the linker's tables (discussed 2026-10-30; items 2 and 3
built, see "The shared-name table is done" below).**
At 512 shared names, step 3's tables come to about 31K with a 16-bit
`int`, not counting the relocations (19K, which B4 removes): `lsyms[]`
11.4K (each function's 32-byte name), `gsyms[]` 10K (20 bytes an
entry), `gnames[]` 6K, and about 3.5K more. That doesn't fit the
Apple II, and 32 objects and 512 names are too few anyway. Options:
1. **Drop the names from `lsyms[]`** (about 10.9K saved): linking
   needs only `fmap[]`; the listing's names would be offsets into
   `gnames[]`, with the current object's local and import names added
   at its end and released after the object. The damaged-file checks
   in `copy_code()` would use `fmap[]` (module offsets). **Decided and
   done** (2026-10-30), with an address fallback for a static name that
   doesn't fit: see "Item 1: the linker's local names" below.
2. **Decided: the shared-name table as parallel arrays**, 11 bytes an
   entry, not a 20-byte struct: name offset (2), type hash (2),
   parameter count or size (2: a function has no size, a global no
   parameter count), offset or import number (2: an import has no
   offset), defining object (1), object of the kept description (1),
   flags (1: data, used, returns, CMDSYS bits). Byte-sized object
   numbers allow up to 255 objects.
3. **Decided: open addressing over those parallel arrays**, for lookup
   speed (linking ccish is about 4,000 lookups; a linear search over
   ~300 names would take on the order of a minute or two in the VM),
   with no extra memory (chaining would need 2 bytes an entry and the
   bucket heads). A name goes in the slot its hash picks, or the next
   free one, wrapping from the last slot to the first; a count of used
   slots says when the table is full, which is only at 100%, so no
   headroom is needed (a nearly full table is slower, not wrong). No
   entry is ever deleted. Entries are then in hash order, not the
   order met, so imports are numbered, and the ESD written, in hash
   order, and every run test's module changes once: accepted as a
   one-time cost (the expected outputs don't change, only the module
   bytes, so the byte-for-byte comparison with the previous commit
   doesn't apply to that commit). Which undefined name is reported
   first also follows hash order.
   The name's hash is used only to pick the starting slot; it isn't
   stored (the `hash` already in the table is the *type* hash). Three
   routines:
   - `gsym_start(name)`: only the starting slot, from the name hash
     (below). Moving to the next slot on a collision needs no `%`:
     add 1, and go back to 0 past the last slot.
   - `gsym_find(name)`: the slot actually holding the name, which may
     not be the starting slot, or -1. It probes from the starting slot
     to the name or an empty slot, and gives up after `MAX_GSYMS`
     probes (a completely full table has no empty slot to stop at).
   - `gsym_add()`: first checks the count of used slots, and stops at
     once with an error if the table is full; otherwise it probes to the
     name or the first empty slot, which must exist.
   An empty slot has name offset -1 (offset 0 is a valid name). The
   listing (`-l`) ends with the slots used and the total, and the name
   bytes used and the total, to show when a limit is getting close.

   **Decided: the name hash is `h = (h + h) ^ c` over the whole name,
   the starting slot is `(h & 0x7FFF) % MAX_GSYMS`, and `MAX_GSYMS` is
   a prime** (discussed 2026-10-30). The hash must be cheap in the VM,
   where `MUL` is a 16-step loop and `SHL` loops once per bit, and must
   spread names evenly, so that a name usually lands in its starting
   slot. A power-of-two size (wrapping by a mask) was rejected: with
   little memory, rounding the table up to a power of two can waste
   nearly half of it. A single `%` per lookup costs about as much as a
   dozen VM ops (`_DIV` is one 16-step shift-and-subtract loop), small
   next to the loop over the name. Masking with `0x7FFF` (`CW $7FFF
   AND`) keeps the hash non-negative for the signed `%`; it is cheaper
   than a rotation, and the VM has no logical right shift (`SHR` is
   arithmetic).

   *Measurements.* A script filled a linear-probing table with each
   candidate hash, using the starting slot above, for two sets of real
   names: A, the 139 shared names declared in `ccish.h`; B, those plus
   every function name in `src/*.c` (385 names). Each result is averaged
   over 21 nearby table sizes, since one size gives noisy results.
   Cells are average probes per successful lookup / share of names in
   their starting slot, for set B:

   | Hash (per character `c`) | VM work per character | 75% full | 90% full |
   |---|---|---|---|
   | ideal (random) | - | 2.50 | 5.58 |
   | `h = (h + h) ^ c` | `DUP ADD`, `XOR` | 2.77 / 60% | 6.20 / 53% |
   | `h = (h << 3) + h + c` (×9) | `SHL` (3 loops), 2 `ADD` | 2.62 / 62% | 5.54 / 55% |
   | `h = (h << 5) + h + c` (×33) | `SHL` (5 loops), 2 `ADD` | 2.47 / 62% | 5.11 / 54% |
   | rotate left 3, then `^ c` | `SHL`, `SHR`, `AND`, `OR`, `XOR` | 2.56 / 62% | 5.99 / 55% |
   | `h = (h << 3) + c` | `SHL` (3 loops), `ADD` | 5.69 / 44% | 9.59 / 41% |
   | sum of characters | `ADD` | 7.22 / 53% | 17.36 / 38% |
   | first 4 characters + length | none (no loop) | 4.04 / 49% | 7.31 / 44% |
   | first 3 + last 2 characters + length | none (no loop) | 2.47 / 62% | 5.00 / 55% |

   Set A is too small to separate the good hashes (most came within
   noise of each other there, even the plain sum), but it also showed
   the prefix-and-length hash and plain shift-and-add as the worst.
   What the table shows:
   - A plain sum is cheapest but fails as the table grows: identifier
     sums cluster near 110 times the length, so they can't fill a
     500-slot table.
   - Plain shift-and-add loses the early characters out of the top of
     the 16 bits (`emit_finish` and `link_finish` collide).
   - A prefix and the length do poorly, because C names share prefixes
     (`parse_`, `emit_`, `fn_`, `var_`).
   - `(h + h) ^ c` is close to ideal at the lowest cost: three trivial
     ops a character, no `MUL` and no `SHL` loop. ×9 and ×33 spread
     slightly better but cost a looping `SHL` each character, and the
     rotation needs an extra `AND` (`SHR` is arithmetic) without
     spreading better.
   - Prefix, suffix and length together do as well as the best, in
     constant time, and the length is free (names are length-prefixed
     in the summary, `read_name()`). But `read_name()` already calls
     `bgetc()` for every character, far more than `DUP ADD XOR`, and a
     successful lookup `strcmp`s the whole name anyway, so hashing only
     part of the name saves little, for more code. Worth another look
     only if the length were kept with each name.

   The table size should be odd, preferably prime: with `(h + h) ^ c`
   the hash's low bit is the last character's low bit, so an even size
   skews the slots a little. Over primes only near 75% full, set B gave
   2.55 probes (ideal 2.50). Needing an odd size wastes at most one
   slot. One weakness: only the last 15 bits remain, so names longer
   than about 22 characters that differ only at the start collide.
   With `MAX_NAME` 31 that is rare, and costs only extra probes, never
   a wrong result. If the spread ever matters more than the cost per
   character, ×33 as `(h << 5) + h + c` is the alternative.
4. **Many objects, through a list file**: the Apple II's 81-character
   command line can't name even 10 objects, so `ccrel -o prog @objs`,
   reading `objs` (one object per line) again in each pass rather than
   storing the names (4 files may be open; ccrel uses 2). About 8 bytes
   an object. *Not decided yet*; likely with step 4.

The limits themselves (names, name bytes) are to be set once B4
measures what memory is free.

**The shared-name table is done** (items 2 and 3, 2026-10-30; 258
tests). Choices made while building it, open for review:
- `MAX_GSYMS` is 509, the prime below the old 512. The table takes
  5.6K (11 bytes a slot), down from 10K. (Later cut to 149 slots and
  1,281 name bytes, once measured: 18.5, "Programs linked and
  measured".)
- The type hash keeps its own array; a function's parameter count and
  a global's size share `g_count[]` (255 or 0 if unknown, by kind).
  The data and returns bits and a new "imported" bit join the old
  flags in `g_flags[]`, and an import's ESD index is kept in
  `g_off[]`.
- Object numbers are `unsigned char`, with 255 (`NO_OBJ`) for none, so
  `MAX_OBJS` can go up to 254. A new entry's description object is
  `NO_OBJ` until `add_global()` fills it in. (ccish doesn't accept
  `unsigned` yet; `link.c` already used it for `fbuf[]`.)
- `add_global()` calls `gsym_find()` first and `gsym_add()` only for a
  name not there, so a table that is exactly full still finds the
  names in it; `gsym_add()` checks the count (and the name bytes)
  before probing, and then needs no `strcmp`.
- The hash masks with `0x7FFF` at every character, not just at the
  end: on a 32-bit host, `h + h` over 31 characters would overflow
  (undefined behavior). The slots are the same, since higher bits never
  reach lower ones; it costs about 2 more VM ops a character.
- The listing ends `; shared names: N of 509 slots, B of 6144 name
  bytes`, after the ESD is written, for single-file `ccish -l` too.
- Every module's ESD now lists its imports in slot order; the
  programs' output is unchanged.
- Tests: `tests/link/probe` (names chosen to collide at slot 508, the
  last, so that they wrap to slots 0 and 1, and names pushed past
  them; a new `MAX_GSYMS` needs new names), the usage line in `basic`
  and `probe`, and two generated links in `run.sh`: 509 names filling
  every slot (a name then found in the full table), and 510 names
  (222).
- **Found while testing (step 3's bug, fixed in its own commit):** the
  whole module could define at most `MAX_FUNCS` (272) functions, since
  `func_at[]`, the RLD's list of function entries, had that size; the
  273rd was reported as a damaged stream file. ccish itself has about
  385 functions. Decided in discussion (2026-10-30): drop `func_at[]`
  (544 bytes) instead of growing it. Step 4 of the final pass now
  rereads each object's `F` records, reading only each one's length
  and skipping the rest, and writes the function entries as it goes:
  part of B4 done early (growing `func_at[]` to ~450 entries, 900
  bytes, was the alternative). *Correction:* this assumed B4's reread
  could write the function entries too, but the loaders want every
  function entry before any relocation, so it can't; step 4 (B4) then
  took the function entries from the summaries instead. `bskip()` now skips the rest of its
  512-byte buffer at a time instead of calling `bgetc()` per byte,
  which speeds every pass's skipping. Output is byte-identical. Test: `run.sh`
  links 300 generated functions in two objects and runs the module,
  calling functions from both ends of each object.

**Item 1: the linker's local names. Done** (decided in discussion
and built 2026-10-30; option A with the address fallback, built
before B4, which it doesn't depend on). `lsyms[]` (272 entries of 42
bytes, 11.4K) is gone; saving about 10.9K. Choices, open for review:
- Steps 2 and 3 read each symbol summary entry into one `Lsym`, as the
  data symbols already were; after step 1, linking needs only
  `fmap[]` and `dmap[]`.
- `copy_code()`'s damaged-file check is now `fmap[idx] == obj_code[k]
  + at`: a function defined in another object maps outside this
  object's code, so it can't match. A function exactly at its object's
  end would map to the next object's first function, so `read_sym()`
  now rejects a function offset equal to the code size (`>=`; a
  function has at least one byte). Data keeps `>`. Before, such a
  summary was caught only by its own object's F record not matching,
  which misses a summary with a defined function that has no F
  record. Test: `run.sh` patches a summary so, and wants the summary
  (not the stream file) named as damaged.
- Names for the listing: `fname[MAX_FUNCS]` (544 bytes), each an
  offset in `gnames[]`, filled only with `-l`. A shared name is the
  one already in the table. A defined `static` function's name is
  added after the shared names, from `lnames_len`, which restarts at
  `gnames_len` for each object, so the table's own count and the
  listing's "name bytes" line are unchanged.
- A static name that doesn't fit is listed by its load address,
  `static@$1012`, where it is defined and where it is called; the
  link never fails because of the listing. Other options were B (a
  separate per-object name pool, about 3K, storing shared names
  twice) and C (no names).
- `map_object()` also runs for the data step, so with `-l` the names
  are built twice per object (cheap).
- Listings and modules are byte-identical to before. Tests: `run.sh`
  fills the name bytes so that a 31-character static name doesn't fit
  (two generated objects, since one compile's name pool can't hold all
  the names) and checks the fallback and that the module runs;
  `tests/link/basic` checks a static name is listed.

**Step 4 (B4) is done** (decided in discussion and built 2026-10-30;
option A). `relocs[]` (2,432 entries of 8 bytes, 19.5K) and
`MAX_RELOCS` are gone, and with them the limit on relocations in a
module (message 189 removed; its number is left unused). Choices, open
for review:
- **The loaders' order.** Both the Apple II loader
  (`src/vmsrc/apple/cmd.pla`, `while ^rld == $02`, then the fixups)
  and plvm (`src/vmsrc/cmdsys.c`, one loop, looking each fixup's
  target up among the entries seen so far) need every `$02` function
  entry before any relocation, but in any order: both keep a list and
  search it linearly.
- **Function entries from the summaries** (`def_entries()`): each
  `.syms` is read again and each defined function gets a `$02` entry
  at its offset. Summaries are much smaller than streams, so this
  replaces the `F`-record pass of the `func_at[]` fix. Entries are in
  summary order (first declaration), not code order.
- **Relocations from the streams** (`reloc_entries()`): after
  `map_object()`, each stream is read again for its `R` records (data
  offsets tracked as in `copy_data()`) and each `F` record's
  relocations; a function relocation to an import is `$91` with its
  ESD index, everything else `$81`. Steps 2 and 3 have checked the
  records; the reread checks only what keeps it in bounds (relocation
  count, function symbol index). Init's call to `main` comes last.
- Relocations are now in object order, each object's data then code
  (before: all objects' data, then all code).
- **Other approach not taken (B):** a temporary RLD file written in
  steps 2 and 3, no reread, but about 20K of disk space and 1.5K of
  buffers while open. Still possible if linking proves too slow.
- **The listing** keeps the current function's relocations in parallel
  arrays `lr_at[]`, `lr_kind[]`, `lr_target[]` (`MAX_FUNC_RELOCS`, 640
  bytes), filled only with `-l`; "(import)" is now found through
  `fmap[]`. Net saving about 18.8K.
- `copy_code()` now checks a function's relocation count against
  `MAX_FUNC_RELOCS` too. `MODULE_BASE` is a macro, used by the header
  and the RLD.
- **Results:** all listings unchanged. 9 test modules changed, each
  only in the order of its RLD entries (checked by a scratchpad script
  comparing the bytes before the RLD, the RLD entries as a sorted
  list, and the ESD); the rest are byte-identical. New test: `run.sh`
  links 2,500 data relocations (25 generated functions of 100 stores
  to one global) and runs the module.
- Disk reads per object now: the stream 4 times (data, strings, code,
  relocations) and the summary 5 times (step 1, data, code, function
  entries, relocations).

### 12. Standard library

Existing programs need at least parts of `stdio.h` (`printf`, `fopen`,
`getc`, `putc`), `string.h`, `stdlib.h` (`malloc`, `atoi`, `exit`) and
`ctype.h`. Write them in C-ish on top of CMDSYS and FILEIO.

- `plvm` implements FILEIO open, read, write, get/set mark and close.
  That's enough to test file-based programs, including the
  self-hosting compiler, under Linux.
- `exit()` from a nested call needs a way to unwind, perhaps the
  existing `longjmp.pla` library.

**`malloc` (sketch, discussed 2026-10-29; for M9).** ccish has no
`malloc` yet. What exists:
- **CMDSYS's heap** (`heapalloc`, `heapallocalign`, `heapmark`,
  `heaprelease`, `heapavail`), which ccish programs can already call.
  It is mark/release: `heapalloc` takes the next bytes off the top, and
  `heaprelease(p)` frees everything from `p` up. A block in the middle
  can't be freed, so it suits memory kept until the program ends, or
  freed in LIFO order. That isn't C's `free()`.
- **PLASMA's `memmgr` library** (`src/libsrc/memmgr.pla`): handles
  (`hmemNew`, `hmemLock`, `hmemUnlock`, `hmemDel`), compaction and
  swapping to disk, with blocks of at most 8K. Its model differs from
  C's: a pointer is only valid while its handle is locked, but
  `malloc`'s memory must never move. (ccish also can't import it yet;
  it imports only CMDSYS.)

The simplest correct `malloc` is a free-list allocator written in
C-ish, about 100–150 lines:
- On first use, take one region from `heapalloc`.
- Each block has a size word in front. Free blocks form a list, kept in
  address order; `malloc` takes the first that fits (first fit) and
  splits off the rest if it is big enough to be useful.
- `free` puts a block back in the list and merges it with free
  neighbors, so that the region doesn't fragment into small pieces.
- `realloc` grows in place when the next block is free, or else
  allocates, copies and frees. `calloc` clears.
- Out of memory gives `NULL`, as C requires.

This gives C's semantics (stable pointers, freeing any block), and it
can't be confused with `heaprelease`, which a program shouldn't then
use on memory below the region. The hard question is the same as for
large locals (open question 4): on a 64K machine, how much of the heap
to claim, against what loaded modules and the frame stack need. Options:
a size the program sets (a compiler or link option), or growing the
region with further `heapalloc` calls while nothing else has been
allocated above it.

### 13. Peephole optimizer

- **A. A window over the last few emitted instructions, in place.**
  Keep a short ring of instruction start offsets. Never optimize
  across a label, so mark labels as barriers.
  - Pros: tiny and fits streaming.
  - Cons: only local patterns.
- **B. Buffer a whole function as an instruction list** (what PLASMA's
  own `codeopt` does), then optimize.
  - Pros: more patterns are possible.
  - Cons: memory proportional to the function size.

Suggest A, and design the emitter so all code goes through one
function that the optimizer can watch.

### 14. Character set and keyboards

Digraphs (`<% %> <: :> %: %:%:`) are standard (C95) and cost almost
nothing in the lexer, so support all of them. The II+ keyboard lacks
lowercase and several characters C uses. Trigraphs (`??<` etc., C89)
cover most of those, but not `_` or lowercase. Options: support
trigraphs as well (cheap, but they occasionally surprise people in
string literals), or treat the //e as the editing machine.
Case-folding C source is not viable, because C is case-sensitive.

Suggest digraphs now, trigraphs behind an option.

Answer: No trigraphs for now. Many don't like them, they surprise
people in strings.

### 15. The compiler's own dialect, bootstrapping and testing

Self-hosting means the compiler's source may use only the features it
supports. Define that dialect explicitly, and check it on the host
(e.g. `gcc -std=c89 -pedantic -Wall` plus our own compiler).

Bootstrap path:
1. gcc builds `ccish` for Linux.
2. `ccish` compiles itself to a REL module.
3. That module runs under `plvm` and compiles itself again.
4. The outputs of 2 and 3 must be byte-identical.

The real Apple II test comes later, once an emulator is available.

Note: the draft code in `src/` uses an expression tree per statement,
plus host-only conveniences (`realloc`, `strndup`, varargs `fatal`).
It does not fit this approach and should be rewritten or treated as a
throwaway prototype.

### 16. Using ccish instead of PLASMA's language

Goal from discussion: be able to use ccish *instead of* PLASMA's own
language, exploiting the VM's features and optimizations.

**Multiple return values** (decided). PLASMA functions can return
several values (`def f()#2`). C can't directly, so ccish compiles a
returned **small struct** as a multi-value return.

- A struct of up to **4** scalar members is returned as that many
  values on the evaluation stack, first member deepest. Larger structs
  use a hidden pointer. (Changed in M6 step 3: a struct of up to 8
  bytes is returned, and passed, as the words of its bytes, which is
  the same thing for `int` and pointer members; a bigger one is an
  error, not a hidden pointer. See "M6 step 3".)
- Pros:
  - It's standard C, so gcc compiles the same source. New syntax such
    as `return a, b;` would clash with the comma operator.
  - It's cheaper than the usual hidden-pointer-and-copy for struct
    returns. This takes "struct returns" off the 64K drop list.
  - It gives interop both ways. Declare PLASMA's `divmod(a,b)#2` as
    returning `struct qr { int q, r; }` and call it. PLASMA code can
    call ccish code the same way.
- Cons:
  - Picking a non-top member out of a call result needs a spill (no
    SWAP).
  - Only `int`, `char` and pointer members.
  - Needs `struct` (M6). Struct pass and return by value move into that
    milestone instead of being dropped.

**Other gaps to close**, in rough priority order:
1. **Importing any PLASMA library.** Replace the hard-coded CMDSYS
   table with C headers generated from PLASMA `.plh` files (a small
   conversion tool for `predef name(args)#n`). Needs the preprocessor
   (M8) and a way to name the module an import comes from (issue 11).
2. **Building library modules.** Export non-`static` functions and
   globals. Let a library stay resident (init returns `modkeep`),
   through a hint or an init-function convention.
3. **Code quality at least as good as PLASMA's compiler:**
   - fused ops (`ADDLW`, `IDXLW`, `DLW`, …);
   - FOR-loop opcodes, `SEL`, `BRAND`/`BROR`;
   - dead-code removal.

   Measure by compiling the same algorithms in both languages, e.g.
   PLASMA's sample programs, and comparing size and speed.
4. **Native code.** Something like PLASMA's `asm` functions, as a
   non-standard escape that gcc ignores.
5. **Costs that C semantics impose and PLASMA avoids:**
   - comparisons used as values must be 0/1, not 0/`$FFFF` (free in
     conditions);
   - `!` can't use `NOT` (the C VM discrepancy);
   - `signed char` needs sign extension;
   - `unsigned` needs extra code.

   Hints (issue 10) may recover some of this, e.g. "this flag is only
   tested".

**Dead code** (decided in discussion): the peephole stage drops
everything after an unconditional exit (`LEAVE`, `RET`, `BRNCH`) up to
the next label. That removes M1's unreachable implicit `CN 0`/`LEAVE`
after a final `return`, without any reachability analysis in the
parser.

### 17. Matching PLASMA's code quality

This section documents how ccish *could* match PLASMA's optimizer; none
of it needs to be built yet.

Sources:
- `src/toolsrc/codegen.c`: `crunch_seq()`, `try_dupify()`,
  `emit_enter()`/`emit_leave()`.
- `src/toolsrc/parse.c`: FOR, WHEN, `AND`/`OR`, RETURN.
- `src/toolsrc/codeopt.pla`, the self-hosted copy of the same peephole
  rules.

#### How PLASMA optimizes

With `-O`, each statement's code is built as a list of ops, and lists
are merged across statements until a label, a `CS` string, or the end
of the function. `crunch_seq()` then rewrites the list in two passes,
each repeated until nothing changes. Only the second pass converts
repeated loads to `DUP`, because a `DUP` can block more valuable
patterns.

The optimizations:

| # | Optimization | Example |
|---|--------------|---------|
| P1 | Constant folding: `+ - * / % << >> & \| ^`, comparisons, `-x`, `~x`, `!x` on constants | `CN 2 CN 3 ADD` → `CN 5` |
| P2 | Immediate forms | `CB 5 ADD` → `ADDI 5`; also `SUBI`, `ANDI`, `ORI` (0–255) |
| P3 | Increment/decrement | `CN 1 ADD` → `INCR`; `CN 1 SUB` → `DECR` |
| P4 | Identities | `+0`, `-0`, `\|0`, `*1`, `/1` removed; `*0` → `DROP CN 0` |
| P5 | Strength reduction | `*2` and `<<1` → `DUP ADD`; `*3` → `DUP IDXW`; `*2^k` → `SHL k`; `/2^k` → `SHR k` |
| P6 | Direct local access | `LLA n LW` → `LLW n`; `LLA n SW` → `SLW n` (and byte forms) |
| P7 | Direct global access | `LA g LW` → `LAW g`; `LA g SW` → `SAW g`; `LA f ICAL` → `CALL f` |
| P8 | Constant address access | `CW $C000 LB` → `LAB $C000` (no relocation) |
| P9 | Address + constant | `LLA n CN k ADD` → `LLA n+k`; `IDXW` → `n+2k`; the same for `LA g` |
| P10 | Fused memory operand | `LLW n ADD` → `ADDLW n`; `LLW n IDXW` → `IDXLW n`; also `ADDLB`, `IDXLB`, `ADDAB`/`ADDAW`, `IDXAB`/`IDXAW` |
| P11 | High-byte load | `LLW n CB 8 SHR` → `LLB n+1` (also `LAW` → `LAB`) |
| P12 | Store then reload | `SLW n LLW n` → `DLW n` (also `DLB`, `DAB`, `DAW`) |
| P13 | Compare and branch fused | `ISEQ BRFLS` → `BRNE`; `ISNE BRFLS` → `BREQ`; `NOT BRFLS` → `BRTRU` |
| P14 | Constant conditions | `CN k BRFLS` → nothing or `BRNCH`; likewise `BRTRU`, `BRAND`, `BROR` |
| P15 | Zero tests | `CN 0 ISNE` → nothing; `CN 0 ISEQ` → `NOT` |
| P16 | Duplicate loads (pass 2) | `LLW n LLW n` → `LLW n DUP`. Not for addresses ≥ `$C000` (hardware). |
| P17 | Short-circuit `AND`/`OR` | `BRAND`/`BROR`, which keep the left value when deciding |
| P18 | FOR loops | Entry test `BRGT`/`BRLT`; loop with `INCBRLE`/`ADDBRLE`/`DECBRGE`/`SUBBRGE`; count kept on the stack and copied to the variable with `DLW` |
| P19 | `WHEN` | `SEL` with a sorted case table |
| P20 | Frames | No `ENTER` when there are no parameters and no locals; then `LEAVE` becomes `RET` |
| P21 | Implicit return | Only added if the last statement isn't a `RETURN` |

A side finding: the cross-compiler's P1 folds comparisons (and `not`)
to **1**, but PLASMA's own run-time true is `$FFFF`. So `3 == 3` gives 1
with `-O` and -1 without. The self-hosted compiler's `codeopt.pla`
folds to `$FFFF`, so the two PLASMA compilers also disagree. This was
verified and is recorded in `../doc/Bytecodes.md` ("Cross-compiler
optimizer: folded comparisons give 1"). For ccish, 1 is the correct C
result.

#### Which are valid for C, and how ccish would get each

Most of these are safe for C and need no hints. Each fits naturally
into one of our pipeline stages:

- **Stage 1 (parser).**
  - P1, constant folding. C needs this anyway, for array sizes, `case`
    labels and global initializers. Use C semantics: 16-bit wrap, and
    an error for constant division by zero.
  - P17–P19 and P21, the statement-level forms. Notes below.
- **Stage 2 (lowering).** Chooses opcodes with knowledge of types:
  - P2–P4;
  - P5, but only `*`, `*2^k` and `<<` (see invalid list);
  - P6–P10;
  - P13 and P15, but only when the next op is a branch.
- **Stage 3 (peephole).**
  - P12, P14, P16.
  - Dead-code removal after `LEAVE`/`RET`/`BRNCH` (issue 16).
  - Any of P2–P10 that only become visible across op boundaries.

  The window must persist across statements, as PLASMA's does, so that
  `x = …; … x …` becomes `DLW`. Labels are the barriers.
- **Stage 4 (emitter), at function end.** P20 (see below), plus
  branch threading (`BRNCH` to a `BRNCH`), which PLASMA doesn't do.

**PLASMA rules that are *wrong* for C, and must not be copied as is:**

1. **`CN 0 ISEQ` → `NOT` (P15).** The C VM's `NOT` returns 1, not
   `$FFFF` (see `../doc/Bytecodes.md`). A `NOT` is still safe in two
   cases:
   - fused into a branch (P13: `NOT BRFLS` → `BRTRU`);
   - masked to C's 0/1 with `ANDI 1`, which gives the same result on
     every VM.

   ccish's `!` uses `NOT ANDI 1`, as suggested in discussion. Never use
   a `NOT`'s raw value.
2. **`/2^k` → `SHR k` (P5).** C division truncates toward zero, but an
   arithmetic shift rounds toward minus infinity: `-3/2` is -1 in C,
   while `-3 >> 1` is -2. It is valid only for `unsigned` operands
   (with masking) or operands known to be non-negative. The same holds
   for the common extra `%2^k` → `ANDI`.
3. **`x >> 8` → high-byte load (P11).** The `>>` is sign-extending, but
   `LLB` zero-extends, so they differ for negative values. It is valid
   for `unsigned`, or for values known to be non-negative.

   It is also valid **whenever only the low 8 bits of the result are
   used**. On the little-endian 6502, `(x >> 8) & 0xFF` is exactly the
   byte at `&x + 1`, whatever the sign. Stage 2 knows how a result is
   consumed:
   - masked with `& k` where `k <= 0xFF`;
   - cast to `unsigned char`;
   - stored into a `char` (a byte store keeps only the low byte).

   In each case:
   - local: `LLW n CB 8 SHR ANDI $FF` (4 ops, 7 bytes) → `LLB n+1`
     (1 op, 2 bytes);
   - global: → `LAB g+1`, relocated with an offset;
   - through a pointer: `<addr> LW CB 8 SHR ANDI $FF` →
     `<addr> INCR LB`, or the +1 folded into a constant offset.

   The same reasoning turns `x & 0xFF` on a word variable into
   `LLB n`/`LAB g`. This does not apply to `volatile` objects, because
   reading 1 byte instead of 2 changes the memory access.
4. **Duplicate loads (P16).** This is invalid for `volatile` objects.
   Standard C already says exactly this, so honour `volatile`, and also
   keep PLASMA's `>= $C000` hardware rule for constant addresses.
5. **Comparison results as values.** C needs 0/1. PLASMA's 0/`$FFFF`
   costs one extra op (`NEG`) in value context, and nothing in a
   condition.

**Statement-level forms in C:**

- **`&&` / `||` (P17).** Use `BRAND`/`BROR` directly in conditions. In
  value context, the result must then be normalized to 0/1.
- **`for` loops (P18), automatically when the shape fits:**
  `for (i = a; i <= b; i++)`, or `<`, `>=`, `>`, `--`, `+= k`, `-= k`,
  with `i` a local or a global. C differs from PLASMA's FOR in two
  ways: C re-evaluates the limit on every iteration, and the body may
  change `i`. Neither is known until after the body has been streamed
  out. A cheap repair keeps this automatic and correct:
  - Decide on the FOR opcodes from the loop header alone.
  - While parsing the body, note whether it assigns `i` or the limit
    variable, or takes their address.
  - If it did, emit a fix-up before the loop-back: `DROP LLW i`
    refreshes the count, and `DROP2 <limit> LLW i` also refreshes the
    limit.

  Untouched loops, the vast majority, pay nothing.
  - `return` inside the loop first drops the loop's two stack values,
    as PLASMA does.
  - `break` exits to code that drops them.
  - `i < n` becomes "`<=` with limit `n-1`", computed once.

  This also needs the `for` side buffer from 4(d).
- **`switch` (P19).** Use `SEL` when the case values span at most
  32767; the compiler sorts the table. Otherwise use a compare chain.
  This is automatic.
- **Implicit return (P21).** Handled by dead-code removal in stage 3.
- **Frames (P20).** In C, locals can be declared at the start of any
  block, so the frame size isn't known until the function ends.
  Automatic fix at function end: if the frame size is 0, delete the
  3-byte `ENTER` (everything after it shifts uniformly, so relative
  branches stay valid; adjust fixup offsets by -3). Then turn each
  `LEAVE 0` (2 bytes) into `RET` plus 1 unreachable pad byte, which
  keeps every later offset the same. (Done, during M5 step 1; a final
  `LEAVE` needs no pad.)

#### Where hints come in (issue 10)

Hints are only needed where C's semantics block an optimization that
PLASMA can do because it ignores those semantics:

| Hint | Unlocks |
|------|---------|
| *non-negative* (a variable or parameter is never negative) | `/2^k` → `SHR`, `%2^k` → `ANDI`, `x >> 8` → high-byte load |
| *flag* (a variable only ever holds a comparison result and is only tested) | Store PLASMA's 0/`$FFFF` and skip the 0/1 normalization |
| *non-recursive* (a function) | Static locals: `LAW`/`SAW` instead of a frame, and no `ENTER`. Measure first; the gain may be small. |

Using `unsigned` gets the first row's benefits without a hint, once
`unsigned` exists (M11). The compiler can also emit the *reverse*
hints, e.g. "`/` by 4 on a signed value: an `SHR` would be possible if
non-negative", so that existing code can be tuned as approach.md's
introduction intends.

#### Measuring

Compile the same algorithms in both languages and compare. PLASMA's
`src/samplesrc` programs and `xplasm -AMO` listings give the baseline.
Track bytecode size per function and, under `plvm -t`, opcode counts
per run. A test can also assert specific listings (e.g. "this loop uses
`INCBRLE`"), so optimizations don't silently regress.

### 18. Classic low-memory techniques we weren't using

Raised in discussion: most of the design so far is conventional
one-pass, low-memory compilation (like Small-C or Turbo Pascal). These
are the classic techniques it was missing. Items 1 and 2 are
**adopted**.

#### 18.1 Streamed output and a separate final pass (adopted; implement early in M3)

The M1/M2 emitter keeps the whole module in memory until the end: all
the bytecode, the data segment, and a fixup table. That can't work for
self-hosting on 64K, where the compiler's own bytecode (20K or more)
can't sit in RAM next to the compiler producing it. Instead, the
compiler streams its output to disk, and a **separate final pass**
turns those streams into the REL module. Memory use during compilation
then no longer depends on program size.

The obstacle is the REL format: data comes before code, and every code
address depends on the data size, which isn't known until the end.
Streams solve that, because the final pass knows all the sizes before
it writes anything.

**During compilation** (the compiler proper):

- **One function at a time in memory.** Each function's bytecode is
  built in a function buffer, so frame sizes and branch targets can be
  patched there (see 18.2). Very large functions fail with "function
  too large". The buffer size is a tuning knob.
- **Two streams** (decided in discussion; a middle ground, see below):
  - **Data stream:** the data segment's bytes in order (string
    literals now; initialized globals later). No tags are needed. Its
    size is the data segment's size.
  - **Code stream:** for each function, its bytecode, followed by that
    function's *relocation records*. They are produced together and
    consumed together. A record gives a code position, a kind, and a
    target, for every address that needs fixing up or an RLD entry:
    - addresses of functions, both defined and not yet defined. They
      are written as 0 or a code-relative offset, because where code
      starts depends on the final data size;
    - imports;
    - data references.
- **Symbol summary**, written when compilation ends: the functions
  (name, code offset, defined or imported, exported), the
  dependencies, and whether there is a `main`. It is small.

Together, these three files are effectively our **object file
format**. They should be defined and kept stable, because the final
pass doubles as a linker (issue 11).

**The final pass** (the writer, or linker) runs after the compiler has
exited or freed its tables, so it has nearly all of memory:

1. Read the symbol summary, so all addresses can be computed.
2. Write the header and dependencies, then copy the data stream.
3. Copy the code stream. As each function's code passes through, apply
   its relocation records (add final addresses) and keep the records
   in memory for the RLD. Generate the init function (`CALL main`,
   …) last.
4. Write the RLD (function entries from the symbol summary, then the
   kept relocation records) and the ESD.

Each stream is read once, sequentially, with no seeking. The listing
(`-l`) is produced here too, since only this pass knows final
addresses.

**Buffering for speed** (decided in discussion). All file I/O goes
through 512-byte block buffers, matching ProDOS blocks, so each ProDOS
call moves a block and not a byte. ProDOS also requires a 1K I/O buffer
per open file. During compilation, three files are open (source, data
stream, code stream), costing about 4.5K. In the final pass, memory is
not tight.

**Why two streams and not one or three** (discussed): one tagged
temporary file uses the least memory during compilation (about 3K),
but the final pass must then read it three times. Three separate
streams (data, code, relocations) are simplest to read, but cost about
6K during compilation. Two streams read each file once at about 4.5K.
Splitting relocations into their own stream can be revisited after
measuring, as long as the formats keep that easy.

**On the host**, the two passes can run in one process, with the
streams in files or memory. On the Apple II, the final pass is a
separate module or program, loaded after the compiler finishes (the
`cmdsys:modexec` "load, run, free" pattern, 18.4). That costs some disk time, and the stream files must
be cleaned up, including after a compile error. File names must fit
ProDOS's 15-character limit, e.g. a base name plus a short suffix.

#### 18.2 Fixup chains (adopted; implement with labels in M3)

Classic one-pass technique (Wirth's PL/0 and Oberon compilers, many
assemblers). A forward reference doesn't get a table entry. Instead,
its operand temporarily holds a link to the previous unresolved
reference to the same target, forming a chain through the code. When
the target becomes known, walk the chain and patch each one. A label
then costs one word (the chain head, or its address once placed), no
matter how many branches use it.

- This applies to references **within the function buffer**: forward
  branches to labels (`if`, loops, `break`, `continue`, `&&`, `||`,
  `?:`, and later `switch` and `goto`).
- References that leave the buffer use 18.1's relocation records,
  because code already on disk can't be patched cheaply.
- PLASMA branch offsets are 2 bytes, so they can hold a link (an
  offset within the function buffer).
- Frame-size bytes (`ENTER`/`LEAVE`) are 1 byte, too small for a
  link. They keep a small per-function list, as now, or are found at
  function end by decoding the function's code.

#### 18.3 Precompiled interfaces instead of parsing headers (with M8/M9)

Turbo Pascal's units: each interface is compiled once into a compact
binary file, and the compiler reads entries only when a name is used.
On 64K, the declarations in real headers (`stdio.h` etc.) could
otherwise fill memory. The CMDSYS table in `parse.c` is a tiny version
of this. Generalize it: generate interface files from PLASMA `.plh`
headers (issue 16) and from our own `.h` files, and look names up on
demand. Decide the details with the preprocessor (M8) and the runtime
library (M9).

#### 18.4 Smaller items (with the 64K fitting work, M12)

(The options for fitting are gathered in 18.6; this section keeps the
details of overlays, native code and symbol tables.)

- **Overlays: what PLASMA actually supports.** Not classic overlays
  (swapping code in and out of a fixed region while the program
  runs). What it has is `cmdsys:modexec("NAME")` (`execmod` in
  `src/vmsrc/apple/cmd.pla`). It loads a module and its dependencies,
  runs its init, and frees it when init returns, restoring the heap
  and the global symbol table, unless init returns `modkeep`. A
  resident module can't be unloaded later: the heap works like a
  stack, and only `execmod` restores the symbol table. Two patterns
  fit:
  - **Load, run, free.** A tiny driver module `modexec`s the compiler
    and then the final pass (18.1). Each gets nearly all of memory and
    is freed on return. This is how the final pass should run on the
    Apple II.
  - **Resident plug-in.** PLASMA's own compiler does this:
    `plasm.pla` runs `cmdsys:modexec("CODEOPT")` for `-O`. The
    optimizer stores the address of its `crunch_seq` into a variable
    the compiler exports, and returns `modkeep`. This suits optional
    features (e.g. the preprocessor, or a bigger optimizer), loaded
    only when wanted. They never give memory back, though.

  Classic p-code systems (e.g. UCSD's segment procedures) swapped
  code in automatically; PLASMA has nothing built in like that.

  **Possible overlay approaches (not committed).** Only pursue these if
  measurements in M12 show the self-hosted compiler doesn't fit. In
  rough order of effort:

  1. **Plug-in overlays, with no change to PLASMA.** Combine the
     resident plug-in pattern with the exported `heapmark()` and
     `heaprelease()`:
     - The compiler exports a table of function pointers.
     - To load overlay A: `mark = heapmark()`, then
       `cmdsys:modexec("OVLA")`. A's init stores its entry points in the
       table and returns `modkeep`.
     - The compiler calls A through the table (`ICAL`) as often as
       needed.
     - To swap: `heaprelease(mark)` frees A; then load B the same way.

     Conditions:
     - The overlay must be the topmost thing on the heap, since anything
       allocated after it is freed with it. ccish's fixed-size static
       tables (module data, not heap) make this hold naturally.
     - Overlays must export nothing, because `heaprelease` does not
       shrink the global symbol table, so exported names would be left
       pointing at freed memory.
     - Each load still adds the overlay's module name to the symbol
       table. That's a small leak, which only matters after many
       swaps.

     Unverified: whether this frees the bytecode on the 128K VM, which
     moves bytecode into auxiliary memory. On a 64K machine the bytecode
     is on the main heap. This relies on loader behavior that isn't
     documented, so it must be tested on 6502 VMs, not just `plvm`.
  2. **A small CMDSYS addition.** Export a "module mark" and a "module
     release" that save and restore both the heap and the symbol-table
     end, which `execmod` already does internally. That makes option 1
     clean and leak-free, perhaps 20 lines in `cmd.pla` per platform.
     The cost: it changes PLASMA, so modules that use it need a PLASMA
     version that has it. It might be offered upstream.
  3. **Our own overlay format**, the closest to p-code segments. The
     final pass (18.1) writes each overlay as bytecode prebuilt for a
     fixed overlay-region address, starting with a jump table. It also
     builds its own 5-byte entry stubs (format in
     `../doc/Bytecodes.md`). The compiler loads overlays into a region
     reserved at startup, with no loader or symbol table involved. This
     is fully under our control, but it's the most work, and the stubs'
     entry address differs per VM.
- **Native code for hot paths.** The self-hosted compiler runs
  interpreted at 1 MHz, so it will need native 6502 helpers for the
  inner loops: reading characters, scanning identifiers, symbol lookup.
  Use hashing instead of linear search there.
- **Compact symbol tables.** Today each symbol holds a 32-byte name and
  about 10 `int`s. Intern names in a string pool, and shrink records to
  the fields actually needed.

#### 18.5 Can ccish compile itself? (assessed 2026-10-29, after M5)

Not yet. Two separate kinds of things stand in the way.

**Language features the compiler's source uses** (5,866 lines in
`src/`, counted with grep):

| Feature | Uses | Planned in |
|---|---|---|
| `#include`, `#define` (30), one include guard | 51 directives | M8 |
| `struct`, with `->` and `.` | 13 declarations, about 500 accesses | M6 |
| `switch` | 26, with 218 `case` labels | M7 |
| `unsigned` / `typedef` / `enum` | 36 / 10 / 13 | M11 |
| `printf` family (varargs) | 31 | M9 library + M11 |
| stdio files (`fopen`, `fread`, `fwrite`, `fclose`, `remove`) | 7 | M9 library |
| `string.h` / `exit` | 59 / 11 | M9 library |
| Nested initializers (`cmdsys_funcs[]`: an array of structs) | a few | M11 |
| `extern`, and linking 9 `.c` files | 11 | issue 11 (decision 20) |
| `long` | 1 (`lex.c`, number overflow) | none: rewrite it |

Nothing else: no function pointers, `goto`, `union` or comma operator.
So, apart from `long` and multi-file linking, the source keeps to the
features planned up to M11, as intended (issue 15). Decided
(decision 20): separately compiled files, statically linked by
`ccrel` (issue 11).

**Memory, the bigger obstacle.** Even under `plvm` the compiler must
fit in 64K: plvm emulates a 64K 6502 memory (`MEM6502_SIZE` in
`src/vmsrc/plvm.h`). With 16-bit `int`s, the fixed tables alone come
to about 66K, before any code:

| Table | Size on the target |
|---|---|
| `Sym` × 512 (`syms`, `globals`, `locals`): a 32-byte name and 12 `int`s, 56 bytes each | ~28.7K |
| Final pass `relocs`: 2048 × 6 bytes | ~12K |
| Final pass `lsyms`: 256 × 40 bytes | ~10K |
| Two 4K function code buffers (`emit.c`, `link.c`) | 8K |
| File buffers, emit relocations, parameter types, labels, types, `tok_text` | ~7K |

Then the code: probably 25–35K of bytecode for this much C (a guess,
not measured), and the PLASMA system itself.

**What follows:**
- Self-hosting stays at M12, after M6–M11, but the space work isn't
  optional along the way: every milestone should keep the tables in
  mind (decision 15), and new tables should be sized for the target.
- The parse pass's tables and the final pass's are never needed at
  once. Running the final pass as a separate module (18.1, 18.4
  "load, run, free") gives each nearly all of memory.
- The `Sym` split (decision 15) is the largest single data saving:
  variables stop carrying function fields, and names can be interned
  (18.4 "Compact symbol tables"). Postponed for now (discussed
  2026-10-29), but needed before M12.
- Measure, don't guess: once ccish can compile most of its own files
  (after M9 or so), compile each one and record its code size, to
  replace the estimate above.

**First measurement (2026-10-29, after M7 and `enum`/`typedef`).**
With gcc's preprocessor standing in for M8 (empty system headers),
`extern` removed and `unsigned` written as `int`, ccish compiles
`peep.c` completely; only linking fails, on a function from another
file. Its 267 lines of code (not counting comments and blank lines)
are **2,019 bytes** of bytecode in 15 functions, about 7.6 bytes per
line, with no M10 optimization yet. (Corrected by the measurement
below: 2,110 bytes. The stream kept after the link error was missing
its last partial block, a `-k` bug since fixed.) At that rate the compiler's 5,008
lines of code would be about **37K** of bytecode, above the 25-35K
guess, with `parse.c` alone (3,014 lines) about 22K. One file is a
small sample (`peep.c` is pattern matching, with many array-of-struct
accesses), so this is a first data point, not a budget. With the
tables (about 66K at 16-bit sizes, above) and PLASMA itself, the
compiler as one program can't fit in 64K, which confirms that the
passes must be separate modules (18.1, 18.4), the tables must shrink,
and code size matters.

The other files stop on what is still missing, in this order: `extern`
and multi-file linking (every file, from `ccish.h`), `unsigned`, then
the library (`NULL`, `printf`, stdio, `string.h`). `link.c`'s globals
alone exceed the 32767-byte data limit (its relocation and symbol
tables).

**All files measured (2026-10-29, option E2 of 18.6).** A script
(kept outside the repository) preprocesses each file with gcc and
small stand-in system headers, then works around what ccish lacks:
`extern` declarations become definitions (or are dropped where the
file defines the name), `unsigned`, `long` and `short` become `int`,
number suffixes are dropped, the `printf` family becomes nothing, the
512-byte local `Bfile`s become static, nested initializer braces are
flattened, `main(argc, argv)` is renamed, and one struct `?:` becomes
an `if`. Every file then compiles; only linking fails, as before.
Bytecode, not counting comments and blank lines:

| File | Lines | Bytecode | Per line | Strings |
|---|---|---|---|---|
| `parse.c` | 3,014 | 19,874 | 6.6 | 7,655 |
| `link.c` | 395 | 2,815 | 7.1 | 866 |
| `lex.c` | 353 | 2,278 | 6.5 | 840 |
| `peep.c` | 267 | 2,110 | 7.9 | 0 |
| `emit.c` | 286 | 2,010 | 7.0 | 512 |
| `lower.c` | 282 | 1,464 | 5.2 | 406 |
| `type.c` | 165 | 869 | 5.3 | 95 |
| `main.c` | 114 | 593 | 5.2 | 152 |
| `bio.c` | 67 | 429 | 6.4 | 50 |
| `name.c` | 73 | 404 | 5.5 | 84 |
| **Total** | **5,016** | **32,846** | **6.5** | **10,660** |

- **Code: about 33K**, below the 37K extrapolation. Split by pass, the
  compiler (everything but `link.c`) is about 30.0K plus 9.8K of
  strings, and the final pass (`link.c` and `bio.c`) about 3.2K plus
  0.9K. `parse.c` alone is 60% of the code and 72% of the strings.
- **Not counted:** the C library ccish's source calls (`printf`,
  stdio, `string.h`; M9), the removed `printf` calls (31, perhaps 300
  bytes), and extra code that real `unsigned` operations would need.
- **Strings: about 10.7K**, almost all messages, as data.
- **Also found:** ccish's own source uses a struct `?:` (`parse.c`),
  `main(argc, argv)`, `short` (a cast in `link.c`), `u` suffixes, and
  `size_t`, none supported yet; and two 512-byte local `Bfile`s that
  don't fit a frame (they must be static, or the buffers smaller).

**Programs linked and measured (2026-10-30, after linker step 4).**
`tools/measure.py` (`make measure`; output in `tools/out/`, not kept)
replaces E2's lost script and is in the repository so it can be rerun.
It preprocesses each file with gcc and the stand-in headers in
`tools/inc/`, applies the workarounds listed at its top (`unsigned`,
`long`, `short` to `int`; suffixes dropped; the `printf` family to 0;
`main(argc, argv)` renamed `main_args`; nested initializer braces
dropped; parse.c's struct `?:` as an `if`), compiles each file with
cccomp, and links the Apple II programs, cccomp and ccrel, with
ccrel, using `tools/libstub.c` (empty C library functions, and a
`main` that calls `main_args`). ccish, both passes in one, is too large
for one module ("module too large") and isn't linked. Results:

| File | Code | Data | Strings | Function symbols |
|---|---|---|---|---|
| `parse.c` | 20,550 | 5,537 | 530 | 274 |
| `link.c` | 5,443 | 16,923 | 631 | 144 |
| `lex.c` | 2,250 | 426 | 358 | 119 |
| `peep.c` | 2,125 | 104 | 0 | 112 |
| `emit.c` | 2,098 | 3,092 | 26 | 121 |
| `lower.c` | 1,508 | 490 | 342 | 117 |
| `type.c` | 1,106 | 784 | 0 | 98 |
| `diag.c` | 671 | 866 | 56 | 113 |
| `bio.c` | 517 | 0 | 8 | 110 |
| `name.c` | 510 | 7,076 | 25 | 108 |
| `ccrel.c` | 232 | 514 | 8 | 112 |
| `cccomp.c` | 212 | 2 | 12 | 112 |

| Program | Code | Data | Strings | In all | Module file | RLD + ESD | Def table |
|---|---|---|---|---|---|---|---|
| cccomp (10 files) | 31,547 | 18,377 | 1,357 | 51,281 | 65,551 | 14,122 | 1,630 (326 functions) |
| ccrel (4 files) | 6,863 | 18,303 | 703 | 25,869 | 29,471 | 3,454 | 415 (83 functions) |

Not counted: the C library (M9), the removed `printf` calls, PLASMA
itself, ProDOS's 1K file buffers, frames and the evaluation stack.
Findings:
- **ccish can't compile two of its own files within its own limits**,
  each only just over: `link.c` needs `MAX_VARS` 74 (72 now);
  `parse.c` needs `MAX_FUNCS` 274 (272), `MAX_PARAM_TYPES` 393 (384)
  and `MAX_POOL` 5,647 (5,632). (Found by bisection, with the other
  limits raised; the stand-in headers' declarations count too.) The
  script compiles such a file with a cccomp built from a copy of the
  sources with raised limits (and links with a matching ccrel), and
  says so.
- **The Apple II loader reads the whole module file into memory**,
  RLD and ESD included, and frees those only after applying them
  (`cmd.pla`, `loadmod`); it keeps 5 bytes per function (the def
  table) while the module is loaded. So cccomp needs about 65.5K just
  to load: it can't load on a 64K machine as one module, before its
  tables or PLASMA are counted.
- **ccrel is 25.9K** (6.9K code, 18.3K data, 0.7K strings), 29.5K
  while loading. Its data is mostly the shared-name table (5.6K) and
  its names (6.1K), sized for 509 names and 6,144 bytes, but linking
  either program uses 131 names and 1,164 bytes (every object declares
  everything in `ccish.h`). Sized 10-20% above that (a prime near 157
  slots, about 1.4K of names), they would take about 3K, saving about
  8.7K.
- **`memcpy` and `memset`:** ccish's own struct copies and array zeroing
  call CMDSYS's, which return nothing; a file that declares the
  standard `void *memcpy(...)` then conflicts at link time with one
  whose generated code calls it. The stand-in `string.h` declares them
  CMDSYS's way. M9's library must settle this.

**Stack and static locals, from the listing (2026-10-30;
`tools/measure.py`, small build).** The script relinks each program
with a `ccrel` that has room for every static name, and reads each
function's frame (`ENTER`), parameters, calls, frame accesses and
`LEAVE`s from the listing. The functions that count nesting (call
`enter_nest()`) are read from parse.c, and `MAX_NEST` from `ccish.h`.
- **Two stacks on the Apple II VM** (`plvm02.s`): `ENTER` takes its
  frame from PLASMA's frame stack (`IFP`); and every bytecode call holds
  **4 bytes of the 6502's own 256-byte stack** until it returns (`CALL`
  pushes the caller's IP, then `JSR`s; the interpreter entry pops only
  the stub's own return address). So call depth, not frame size, is
  the tight limit: about 50-60 levels at most, less what ProDOS and the
  system use.
- **cccomp's worst case** from `(init)`, each nest-counting cycle taken
  32 times: at least 14,288 frame bytes and 472 calls (1,888 bytes of
  6502 stack). A loose upper bound (the heaviest path each time), but
  even the bounded part is too deep: one level of expression nesting
  is about 4 calls, so `MAX_NEST`'s 32 levels allow about 128 calls,
  512 bytes of 6502 stack. **The nesting limit the Apple II can afford
  is set by the 6502 stack, and is far below 32.**
- **Recursion with no nesting count** (no bound but the input): prefix
  operators (`parse_unary`, as in `!!!!x`); statements (`parse_statement`,
  `parse_if`, the loops, `parse_switch`: an `else if` chain nests as deep
  as it is long; `loop_body()` counts loops but not `if`); `init_flat`,
  `holds_const`; and recursion over types (`type_size`, `qualify`,
  `type_hash`), bounded by the type table. The cycles through the
  error path (`fatal`, `stop`, `clean_up`) and the stage pipeline
  (`put`, `ir_put`, `lower`, `peep_put`, `emit_put`, ...) are in the call
  graph but not real recursion.
- **Next (proposed):** measure the real depth: build the host cccomp
  with call tracing and compile ccish's own source, recording the
  deepest call chain and its frames (the host runs the same code); then
  set `MAX_NEST` (and count nesting in `parse_unary` and in statements)
  from that, as the other limits were set.
- **Static locals** (the idea in 18.6 C1, "Static locals for leaf
  functions"): in cccomp, 85 of 317 framed functions are leaves (286
  frame accesses; a 10-byte region would do); with new opcodes they
  would save 262 bytes, with `LAW`/`SAW` they would cost 217. A
  compiled stack covers 271 functions (2,442 accesses; a 382-byte
  region): 844 bytes saved with new opcodes, 2,319 lost with
  `LAW`/`SAW`. Plus about 45 cycles a call each, with new opcodes. So:
  worth it for speed, modest for size (under 3% of cccomp's code), and
  only with new opcodes. Static locals don't reduce the 6502 stack (the
  4 bytes a call remain). In ccrel: 160 bytes saved (all 75 framed
  functions), with new opcodes.

**Then (2026-10-30, decided in discussion):** ccish's four limits were
raised 10% above those peaks ("Table sizes", below), and ccrel's
shared-name table cut to 10% above linking's use: `MAX_GSYMS` 509 to
149 (the first prime at least 10% above 131) and `MAX_GNAMES` 6144 to
1281 (1,164). Measured again: ccrel 17,178 bytes (6,855 code, 9,620
data, 703 strings; module file 20,780), down 8.7K; cccomp 52,359
(data 19,455; module file 66,629). No file needs raised limits now.
The tests that filled the old table were resized (149 names fill it;
`tests/link/probe` has new names that collide at slot 148; the
function-count test uses 324 static functions; the static-name
fallback test fills 1,281 bytes).

#### 18.6 Options for fitting in 64K (listed in discussion, 2026-10-29)

A set of options, not a plan; any fitting will combine several. Where
the memory goes today (16-bit sizes): code about 37K (extrapolated from
`peep.c`, 18.5), tables about 66K, and about 11K of string literals,
mostly messages (8K in `parse.c`), against "well under 40K" free on a
64K Apple II (issue 1, not yet measured).

**A. Names.**
- **A1. Interned pool (today):** each distinct name once, in full;
  tables hold a 2-byte offset. Exact, as C requires, but costs the total
  length of all distinct names (several K for ccish's own).
- **A2. Forth-style keys:** length plus the first *k* characters
  (fig-Forth used 3) as a fixed key, no pool. 4-8 bytes per name, fast
  compares. But different names can share a key (`tok_text` and
  `tok_type` with length + 3): a confusing "redeclaration" in one
  scope, and **silently wrong code** across scopes (a local hiding an
  unrelated global).
- **A3. Forth key plus a hash of the whole name** (e.g. length, 3
  characters, 16-bit hash: 6 bytes). Collisions become rare, but are
  still silent unless checked some cheaper way.
- **A4. Fewer significant characters** (e.g. 8 or 12, not 31), still in
  the pool. C89 itself guarantees only 6 for external names, so this
  is a documented, C-sanctioned kind of limit; programs must keep names
  distinct that far.
- **A5. Free names no longer needed** (members and enum constants at a
  file's end; locals already go at each function's end).
- **A6. Identifiers as numbers** (discussed 2026-10-30; for later,
  only if about 6K more is needed after 18.7 step 3 and E1): `ccpp`
  numbers each distinct identifier, and `cccomp` compares numbers and
  keeps no spellings. The name pool is `cccomp`'s biggest table (name.c
  has 7,824 bytes of data, mostly `MAX_POOL`, 6,212). A message looks
  a spelling up in a file only when it is printed, as with
  `ccish.msg` (C2). To design first: type hashes include struct tag
  names and must agree between files (so keep a hash of each tag's
  spelling, not its number); CMDSYS functions, `main`, `memcpy` and
  `memset` are recognized by spelling (fixed numbers from `ccpp`?);
  `.syms` and `ccgen` (`-i`, its messages) need function and global
  names; and `ccpp` has at most 4 files open, so names can't simply go
  to a file of their own.

**B. Tables.**
- **B1. The `Sym` split** (decision 15): function and variable tables
  of parallel arrays with `char` fields and name keys; about 29K to
  perhaps 4-5K. The largest data saving, but a broad parser edit.
- **B2. Limits sized to ccish's largest file** (functions, globals,
  locals, labels, parameter types, types), not round numbers. Easy and
  measurable; bigger programs get a clear error.
- **B3. The final pass as its own module** (load, run, free; 18.1, 18.4):
  its 22K of tables and about 3K of code never share memory with the
  parser's.
- **B4. Stream relocations instead of holding them:** read the input
  twice instead of keeping 2,048 relocations (12K). Slower on floppies.
  **Done** (2026-10-30, linker step 4; see issue 11, "Step 4 (B4) is
  done"): about 18.8K saved.
- **B5. Smaller or no code buffers:** two 4K function buffers to 2K (a
  function-size limit; ccish's largest so far is about 1K), or write
  code straight to disk and patch it by seeking (slower).
- **B6. Fewer open files:** ProDOS needs a 1K buffer per open file, and
  today a compilation has the source and 4-5 stream files open (5-6K).
  One combined stream file makes it 2K, but changes OBJECT.md.
  **Decided: B6a** (discussion, 2026-10-29; decision 18). **Done**
  (2026-10-29; see "B6a is done" under Milestones). Found before:
  - **It is required, not just a saving.** PLASMA's Apple II file
    library (`src/libsrc/apple/fileio.pla`) allows at most 4 open
    files (`MAX_IOBUFS`), the system buffer plus up to 3 more taken
    from the heap with `fileio:iobufalloc`. The compile pass keeps 5
    open (the source, `.data`, `.drel`, `.strs`, `.code`), so it can't
    run on the Apple as it is. (`.syms` is written after those close.
    The final pass opens at most 3: an input, `.drel` beside `.data`,
    and the module.)
  - Each open file also has a 512-byte `Bfile` block buffer; emit.c's
    five are static, about 2.6K in cccomp whether open or not.
  - Variants considered: **B6a**, one tagged stream (2 files open while
    compiling); **B6b**, `.code` plus one tagged file for data,
    relocations and strings (3 open, about 1K less saved); **B6c**,
    raising `MAX_IOBUFS` upstream (no saving: each file still costs
    1.5K); **B6d**, holding data and strings in memory (spends what it
    saves).
  - **B6a as decided:** the compile pass writes one stream of tagged
    records (data, data relocation, string, function), with each data
    relocation in the same stream as the data it patches, so the final
    pass never needs two inputs open (which helps B4 too). `.syms`
    stays a separate file, written last: the final pass needs it first
    for the layout, and a separate file avoids seeking back to patch a
    header. The final pass reads the stream three times (data, then
    strings, then code), skipping the other records. OBJECT.md's format
    version goes to 3. One object file per `.c` is also the shape the
    linker (issue 11) wants.
  - **Disk space:** about the same as today, not more. Tags add a
    byte, or a byte and a length word, per data, string or relocation
    record: a few hundred bytes on parse.c's 37K of streams. One file
    instead of four saves the partly filled last blocks and the index
    blocks of the others (2-5 blocks). See E3.

**C. The compiler's code.**
- **C1. Two builds from one source** (`#if`, after M8): the cross build
  (gcc) keeps all checking; the self-hosted build drops checks that
  don't change generated code, since its input is checked by the cross
  build and by gcc (decision 16). Candidates: `const` and qualifier
  checks and warnings, strict pointer-compatibility errors,
  redeclaration and duplicate checks, the listing (`-l`) and IR dump
  (`-i`). Possibly 15-25% of `parse.c` and much of the messages. But the
  self-hosted compiler then accepts some invalid programs, perhaps
  miscompiling them silently, and there are two builds to test.

  **Two builds: the plan** (decided in discussion 2026-10-30; decision
  21). More aggressive than the C1 sketch above:
  - **Macros.** One switch, `CCISH_SMALL`, given on the command line
    (`-DCCISH_SMALL`). `ccish.h` turns its absence into feature
    macros, which the code tests: `CCISH_LISTING` (`-i`, `-l` and
    everything only they use) and `CCISH_CHECKS` (diagnostics a valid
    program never triggers). Each can be turned off alone, to measure
    or debug it. The full build is the default.
  - **The premise:** what the small build compiles has already been
    checked by the full build, or by gcc. So it may silently accept
    some invalid programs (a type error may compile into wrong code);
    it must never mishandle a valid one.
  - **Kept in the small build:** undefined identifiers, and undefined
    functions and globals at link time (required); syntax errors (the
    parser must stop); "not supported" constructs (else ccish would
    quietly compile something different); table overflows and
    function-size limits (the compiler's own memory safety); the
    damaged-object checks that bound array indexes in `ccrel`; file
    errors.
  - **Dropped (`CCISH_CHECKS`):** type compatibility (pointer and
    integer mismatches, incompatible pointers: `check_assign`,
    `compatible`); `const` and qualifier enforcement and all warnings;
    redeclaration consistency (conflicting prototypes, duplicate
    definitions in a file); link-time type checking (`type_hash`, and
    `ccrel`'s conflicting-types checks: a hash of 0 already means
    "unknown"); lvalue and similar misuse checks, where the code
    generator can't be hurt by them; internal-error checks.
  - **Dropped (`CCISH_LISTING`):** `-i` (about 0.5K in cccomp) and
    `-l` (about 3K in `ccrel`), and with them every `printf` and
    `sprintf` use, so the small build needs no `printf` at all.
  - **Same limits in both builds,** so the full build reports what
    would overflow on the Apple II.
  - **Tests:** every run test is compiled by both builds, and their
    objects and modules must be byte-identical (the main guard against
    an `#ifdef` that drops too much); error tests whose check the small
    build drops are marked, and run only with the full build.
    `make measure` measures the small build.
  - **Preprocessor:** until ccpp (M8) exists, gcc's preprocessor makes
    the small build (`measure.py` already uses it). ccpp must handle
    `#define`, `#ifdef`, `#ifndef`, `#else`, `#endif`, and probably
    simple function-like macros.
  - **Style:** whole check functions go under `#ifdef`, with empty
    stand-ins in the small build; `#ifdef` blocks at call sites only
    where a check is inline.
  - **Order:** (1) `CCISH_LISTING` with the two-build test harness; (2)
    `CCISH_CHECKS`; each measured, each its own commit. (3) Then split
    `parse.c` in two: its peaks set `MAX_FUNCS`, `MAX_POOL` and
    `MAX_PARAM_TYPES`, so a split may let those limits (and cccomp's
    tables) shrink, and it helps the floppy-space problem (E3).

  **Step 1 (`CCISH_LISTING`) is done** (2026-10-30). Choices, open for
  review:
  - In the small build, `opt_list` and `opt_ir` are the constant 0
    (`ccish.h`), so code that only passes them on (`add_reloc()`'s
    `list`) needs no `#ifdef`; code that calls a listing function or
    `printf` has one. Parameters only the listing uses are marked
    `(void)` in the small build (no code).
  - Gone from the small build: the IR dump (`dump_ir()`, `ir_names`)
    and `-i`; the listing (`list_code()`, `list_table()`,
    `reloc_note()`, `func_label()`, `list_name()`, `keep_reloc()`,
    `opinfo`, `fname[]`, `lr_*[]`) and `-l`; and their usage text.
  - The Makefile builds `cccomp-small` and `ccrel-small` for the host
    (`make small`, part of `make test`). `run.sh` compiles every run
    test and every link test that links with both builds, and wants
    byte-identical objects, summaries and modules (checked to fail on a
    one-byte difference). `make measure` now measures the small build
    (`tools/measure.py -full` the full one).
  - **Measured** (small vs full): cccomp 51,802 bytes vs 52,425 (623
    less: code 180, data 104, strings 339); ccrel 13,627 vs 16,820
    (3,193 less: code 896, data 1,711, strings 586), and its module file
    16,509 vs 20,422. Both builds' figures leave out the `printf` calls'
    arguments (`measure.py` makes them 0), so the real savings are a
    little larger.
  - **No `printf` family in the small build** (done next, its own
    commit; asked for in discussion): diagnostics (diag.c `report()`)
    and the usage messages now write with `fputs`; numbers go through
    `num_text()` (diag.c: decimal by hand, never negating, so the most
    negative int works); parse.c's four `sprintf`s became `strcpy` and
    `strcat`. In both builds, with the same output. The small programs
    link nothing of the `printf` family (`nm`); the full build keeps
    `printf` for the listing. `num_text()`'s buffer is static, and the
    diagnostics use it too, so a caller copies it before passing it as
    a detail. New test `tests/errors/case_duplicate_negative.c`.
  - **Direction after measuring (discussed 2026-10-30):** the user
    wants to *add* to cccomp (at least `unsigned`), and it is already
    too big. Most of what it does, ccish's own source needs (structs,
    initializers, `switch`, `enum`/`typedef`), so dropping features
    would block self-hosting. **Agreed: move the back end (lower.c,
    peep.c, emit.c: about 6.3K code and 3.6K data) into a program of
    its own**, so the pipeline is ccpp, front end, back end, ccrel
    (D3); the front end writes its IR stream to a file. Designed
    2026-10-30, before steps 2 and 3 (the user's choice of order), as
    18.7, with the IR file's size for parse.c measured there. Also
    planned: per-stage headers, since every file pays
    about 2.5K of name pool for `ccish.h`'s declarations.
  - **Static locals for leaf functions (discussed 2026-10-30; an idea
    for later, possibly with new VM opcodes):** leaf (or near-leaf)
    functions that don't recurse keep their locals in a small fixed
    region instead of a frame, so they need no `ENTER` and end in
    `RET`: about 45 cycles and 4 bytes saved a call (`ENTER n,0` about
    30 cycles and 3 bytes, `LEAVE` about 20 and 2, against `RET`'s 6
    and 1).
    - **With today's opcodes** (`LAW`/`SAW`/`DAW`): each access costs
      more. In the Apple II VM (`plvm02.s`) `LLW` and `LAW` both load
      indirect-indexed (`LDA (IFP),Y`, `LDA (TMP),Y`), but `LAW` first
      copies its 2-byte operand to `TMP`: about 53 cycles against 40,
      3 bytes against 2 (plus a 4-byte RLD entry each, in the module
      file only). Break-even is about 4 accesses (size) and 3-4 a call
      (speed); parameters would each need a `SAW` (about 53 cycles, 3
      bytes) instead of `ENTER`'s copy loop (about 29 cycles each). A
      win only for functions that touch their locals a few times.
    - **With new opcodes** (2-byte loads and stores with a 1-byte
      offset into a fixed region, `LDA REGION,Y`: 4 cycles, not 5): no
      cost per access, so the saving per call is clear; one more op
      could store a function's parameters into the region. But every
      PLASMA VM (6502, C, JIT) would need them: an upstream question,
      like SWAP (issue 21).
    - **Generalized,** this is a "compiled stack" (as in C compilers
      for the 8051 and PIC): every function that can't recurse (and
      isn't called through a pointer) gets static locals, and
      functions never active at once share the same bytes. That needs
      the whole program's call graph, so it belongs in `ccrel`, which
      sees every call as a relocation. It also bounds the stack.
    - **Next:** measure the benefit from the listing (leaf and
      near-leaf functions with locals, and how often they touch them),
      with the stack estimate, which needs the same call graph.
  - **Measured after it:** small cccomp 52,272, ccrel 13,966; full
    52,900 and 17,164. These are higher than above (51,802 and
    13,627), but only because `measure.py` had made every `printf`
    call 0, so printing diagnostics counted as almost nothing, and the
    `printf` library itself not at all. The new figures include all of
    the printing; the small build no longer needs a `printf`.
- **C2. Smaller messages:** shorter text (11K to perhaps 5K), or error
  numbers with the text in a disk file read only on an error (to about
  1K), or numbers only, explained in the documentation.
- **C3. Drop C semantics that cost compiler code**, with "not
  supported" (or a simpler accepted form) instead. Candidates:
  block-scope tags, typedefs and enum constants (file scope only);
  implicit function declarations (require prototypes; adopted,
  decision 20); the exact
  compound-assignment ordering (`compound_elem`, `reversed_op`);
  rotation in place (`K_STORE`, stage 2's buffer; this makes generated
  code bigger); pending-constant folding beyond what constant
  expressions need; `const`-variable folding (`has_val`); the
  address-constant parser for global initializers (just strings and
  `&name`); struct by value (ccish doesn't use it; struct assignment
  stays, since it does); C99 `for` declarations; digraphs; `sizeof` on
  expressions (only `sizeof(type)` and `sizeof variable`). Each is code
  simply gone, but real C hits more "not supported" errors, and some
  make the generated code (the compiler's own too) bigger.
- **C4. Optimize the generated code (M10):** fused opcodes (`ADDLW`,
  `IDXLW`, ...), peephole patterns, shared sequences. Shrinks every
  program, the compiler too; gain unknown (perhaps 10-20%), and the
  optimizer is more code. **Measured 2026-10-30** (cccomp's own code,
  from `make measure`'s listing): the obvious fusable patterns come to
  only about 0.9K (3%), so 10-20% was too high. Most of the code is
  `CALL` (5.5K), `LLW` (5.2K), branches (about 4K) and `ENTER`/`LEAVE`
  (2.1K). **Done the same day** (decided in discussion): `ADDI`/`SUBI`
  for a byte constant added or subtracted either way, and `ADDLW`,
  `IDXLW` (and the byte and global forms) for a variable's value added,
  in peep.c (`rewrite()` split into four functions to stay under
  `MAX_FUNC_CODE`; the fused ops found by their opcode layout, not a
  table, to keep peep.c small). cccomp's code: 31,547 to 31,560 (other
  files shrink about as much as peep.c grows); ccrel's: 6,863 to 6,667.
  `MAX_IDS` 240 to 268 (`ccish.h`'s new opcode constants; parse.c
  needs 243). **Found:** the biggest pattern, `NEG` before a branch
  (437 bytes), comes from `&&` and `||` in conditions, compiled as
  values (`a NEG BROR L b NEG L: BRFLS`); the existing `NEG BRFLS`
  rule can't apply across the label. The fix is in the parser
  (conditions as jumps), not the peephole stage.

  **Conditions as jumps (decided in discussion 2026-10-30; built as its
  own commit, so it can be dropped on its own).** `&&`, `||` and `!` on
  them compile to branches, not to a 1 or 0, unless the value is used.
  Walked through in review: `if (a > 5 && (b == 0 || c))` goes from
  24 bytes of condition to 17. The design:
  - **`K_JUMP`**, a new Operand kind: a condition whose code is
    emitted, which **jumps to its false label** (`val`) **when false
    and falls through when true**. Every `&&` and `||` result has this
    one form, so most nestings never have two labels to merge.
  - **`a && b`**: `a` jumps on false to the result's false label (a
    `K_JUMP` `a` already is one: its label is reused), then `b` does
    too.
  - **`a || b`**: `a` jumps on true to a label `T`; `b` jumps on false
    to the result's false label (a `K_JUMP` `b`'s own); `T` is placed
    after `b`, where the true outcome falls through. A `K_JUMP` `a`
    needs `BRNCH T` then its false label, where `b` starts.
  - **`JOIN a b`** (new IR op and pseudo-op, no bytes): label `a`'s
    waiting branches go to label `b` instead, by moving them from `a`'s
    fixup chain to `b`'s in stage 4, or resolving them at once if `b`
    is placed (a backward target). Needed only when both sides bring
    a false label: `&&` with a parenthesized `||` (or `!`) on its right,
    as in `a && (b || c)` (about 10 places in ccish's source), and when
    a condition's false label goes to a statement's (`if`'s else).
  - **`!` on a `K_JUMP`**: `BRNCH G`, then its false label (where true
    now falls through); `G` is the new false label.
  - **Consumers:** `if`, `while`, `do`, `for` and `?:` take a `K_JUMP`
    through `cond_branch()`. Any other use (`x = a && b`, `return a ||
    b`, an argument, an operand of another operator) turns it into a
    value in `rvalue()`: `CN 1 BRNCH E F: CN 0 E:`. That is a few bytes
    more than today's form, but such uses are rare. `parse_expr()`
    always calls `rvalue()` on a left operand before parsing the right
    one, so the value is made before any later code. As a statement,
    `discard()` just places the false label.
  - **Comparisons don't change:** a comparison is still a value
    (`ISxx NEG`), but now it comes right before its branch, so the
    existing peephole rules make `ISEQ NEG BRFLS` into `BRNE`, `CN 0
    BREQ` into `BRFLS`, and drop the `NEG`. An operand that isn't a
    comparison (`c` in `b || c`) is tested as it is: no `CONST 0 NE`.
  - **Two new peephole rules, at a label:** `BRNCH L` then label `L` ->
    the label alone; and `Bcond L; BRNCH T; L:` -> `Binv T; L:` (the
    inverted branch, for `BRFLS`/`BRTRU` and `BREQ`/`BRNE`). The second
    removes the `BRNCH` that `a && b || c` and `!(a && b)` need, in the
    usual case where the last test is a branch. Labels are barriers, so
    these run just before the window is flushed for a label.
  - `IR_BRAND`, `IR_BROR` (and `OP_BRAND`, `OP_BROR`) are no longer
    used, so they go.
  - **Costs:** perhaps 0.4-0.6K more code in parse.c, no new tables;
    more labels per function (`||` uses two), which may need
    `MAX_LABELS` raised. Estimated gross saving about 1-1.3K in
    cccomp, so a net 0.5-0.8K, and in every program. To be measured,
    and dropped if it doesn't pay.

  **Built (2026-10-30), with a refinement found while measuring.** The
  design as reviewed (v1) took cccomp's code from 31,560 to 31,309
  bytes and ccrel's from 6,667 to 6,515; the `NEG`s before branches
  are gone (42 remain, from values). But `BRNCH` grew (486 to 546): in
  `!(a || b)` and `do ... while (a || b)`, the `||`'s true label was
  placed at once, between its last branch and the `BRNCH` that `!` or
  `do` adds, and a `JOIN` flushed the peephole window; either one
  stopped the inverted-branch rule. The refinement, kept (decided in
  discussion: better code for every program is worth it):
  - **A pending true label:** a `K_JUMP` may also carry a true label
    (`off`, or -1) that isn't placed until the code goes on along the
    true path (`place_true()`). `||` leaves its `T` pending; `!` makes
    it the new false label; `do`'s `cond_branch()` joins it to the
    loop's top. So `!(a || b)` is `a BRTRU G b BRTRU G`, and `do ...
    while (a || b)` branches back from both tests.
  - **`&&` keeps its right side's false label** when that side is a
    `K_JUMP`, and joins the left side's to it, so the label placed next
    is the one the last branch names.
  - **`JOIN` waits in the peephole window** instead of flushing it: it
    emits no code, it is passed on in order (stage 4 needs the branches
    it moves first), it is kept in dead code, and the label rules skip
    it (`code_before()`).
  - **Measured:** 31 fewer `BRNCH`es in cccomp (about 93 bytes), but
    about 170 bytes more compiler code (parse.c and peep.c), so
    cccomp's code is 31,454 (145 more than v1, 106 less than before);
    ccrel's 6,497 (18 less than v1). Every other program only gains.
  - **Tests:** `tests/run/logical_truth.c` (ten `&&`/`||`/`!` forms as
    conditions and as values, all 8 cases, with operands 7, -1 and 2
    as well as 0), `logical_flow.c` (order and short-circuiting in
    each statement, `?:`, expression statements, values, constant
    operands, `sizeof`), and `logical_code.c` (the exact code of seven
    functions: no `NEG`, `BRNCH`, `ISEQ`, `ISNE`; counts of `BRTRU`,
    `BREQ`, `BRNE`). Expected outputs written by hand, checked with
    gcc (which differs only in `sizeof(int)`).
  - Remaining `BRNCH`es come mostly from `for` loops (the step is
    placed before the body: "Later" in IR.md) and from `else` parts.
- **C5. Restructure ccish's own source for size:** smaller functions,
  table-driven code instead of `switch` chains, `ccish.h` split by
  stage. No language change; gains hard to predict.
- **C6. Address constants from the expression parser** (discussed
  2026-10-30; for later, after 18.7 step 4, A): global initializers
  have their own parser for address constants (`parse_addr()`, 592
  bytes). Instead the expression parser could make an "address
  constant" operand (a data or string symbol plus an offset, which
  constant folding adds to and subtracts from), which an initializer
  accepts, and which code elsewhere pushes with `DATAADDR`/`STRADDR`.
  Perhaps 0.3-0.4K net, with no feature lost (any address the folder
  understands becomes valid, unlike C3's "simpler `parse_addr`").
  Feasibility to check against `&` and the pending operands.

**D. Program structure.**
- **D1. One program** (the preference in issue 1): simple and fast, but
  needs aggressive versions of A-C, and may not be reachable.
- **D2. Compiler plus final pass as two modules** (B3): little work,
  already planned.
- **D3. More passes:** preprocessor to a temporary file, parser to an IR
  file, then a backend. Each fits easily, as in many small-machine C
  compilers, but floppy I/O makes it much slower. Adopted (2026-10-30):
  the preprocessor (decision 2) and the back end (`ccgen`, 18.7) are
  programs of their own.
- **D4. Overlays** (18.4's three approaches): one logical program, but
  relies on undocumented loader behavior to test on 6502 VMs.
- **D5. Fall back to 128K** (issue 1, option A): bytecode in auxiliary
  memory; probably fits with modest work, but gives up the 64K II+.

**E. Measure first.**
- **E1.** Measure the real free memory on a 64K PLASMA system (open
  since issue 1). It sets how much of the above is needed.
- **E2.** Measure every file's code size with the same workaround as
  `peep.c` (18.5), instead of extrapolating from one file. Done
  (2026-10-29; results in 18.5): about 33K of code (30K for the
  compiler, 3K for the final pass) and 10.7K of strings. Redone
  2026-10-30 with `tools/measure.py`, which also links the programs
  (18.5, "Programs linked and measured").
- **E3. Disk space** (measured 2026-10-29, after C2, with E2's
  self-compile). A 5.25" floppy holds 140K (280 blocks, about 273
  free for files). ProDOS stores each file in whole 512-byte blocks,
  plus an index block for any file over one block.
  - **parse.c is 145,133 bytes: it doesn't fit on a floppy by itself.**
    It is the main disk problem, with or without B6.
  - parse.c's streams: `.code` 30,644, `.data` 5,482, `.strs` 530,
    `.drel` 300 (about 37K); the module is about the same size.
  - The most disk needed at once is the source plus its streams while
    compiling, then the streams plus the module in the final pass. B6
    changes neither.
  - **Uninitialized globals are stored as zeros.** link.c's `.data` is
    29,359 bytes, mostly its zero-filled tables. PLASMA modules have no
    zero-filled (BSS) section, so the zeros are in the module file too.
    They cost disk, not memory: loaded, they take the same memory
    either way.
  - How much of the source is comments and indentation (bytes as is;
    with leading spaces as tabs; without comments; without comments
    or indentation; the script is in the session scratchpad, not the
    repository):

    | File      | As is   | Tabs    | No comments | Neither |
    |-----------|---------|---------|-------------|---------|
    | `parse.c` | 145,133 | 133,324 | 80,268      | 61,973  |
    | `ccish.h` | 17,501  | 17,137  | 6,543       | 6,231   |
    | `link.c`  | 18,262  | 16,729  | 13,188      | 10,576  |
    | `lex.c`   | 11,049  | 9,558   | 8,552       | 6,114   |

**F. Disk space** (E3; options. F2 partly adopted, F3 rejected, F8
decided for later; the rest not chosen yet). **parse.c's size**
is the main problem. Splitting it is one way, but not the only one:
- **F1. Split parse.c into several files.** Each file and its streams
  are smaller. It needs `extern` and multi-file linking (M8, issue 11),
  which self-hosting needs anyway. But `ccish.h` is read again for
  each file (18.3 would help), and more files mean more steps to build.
- **F2. A stripped copy for the Apple,** made by a tool from the
  commented source, which stays the one maintained by hand. Comments
  are about 45% of parse.c (145K to 80K; 62K without indentation too).
  But what the Apple compiles is no longer the source people read, and
  error line numbers then refer to the copy, unless the tool keeps
  lines (blank comment lines and all, which keeps less of the saving).
  Partly adopted (2026-10-29): the separate preprocessor's `.i` file
  is such a stripped copy, made on every build, with `#line` keeping
  the line numbers right (issue 2, "Revised"). It shrinks what the
  compile pass reads, but not the source on disk.
- **F3. Tabs for indentation:** (rejected 2026-10-29: 2-space indents
  instead, decision 19, saving about 6%.) 8 spaces to a tab saves about 8% of
  parse.c (145K to 133K). Simple, and the source is unchanged
  otherwise, but alone it's not enough, and it changes the project's
  style.
- **F4. Shorter comments in the source:** move long design
  explanations into approach.md and keep the comments brief. It shrinks
  the one source everyone reads, but it is hand work, and much of
  parse.c's commentary is useful where it is.
- **F5. Smaller code:** C3 (dropping costly C semantics) and C5
  (restructuring) shrink the source as well as the compiled code.
- **F6. Zero-filled globals as a count:** the compile pass writes a run
  of zeros as a length instead of bytes, and the final pass expands it.
  It shrinks the streams (link.c's `.data`: 29K to well under 1K), but
  not the module, unless PLASMA's loader learns a zero-filled section
  (an upstream change).
  - **F6a. A zero-filled block without a loader change** (discussed
    2026-10-30; to do soon, after 18.7 step 3, with E1). Found: 19,283
    of `cccomp`'s 19,535 data bytes are zeros (measured from the
    self-compile's `D` records), all in the module file. The Apple
    loader (`loadmod()` in `src/vmsrc/apple/cmd.pla`) reads the whole
    file onto the heap (`allocheap()`, a bump allocator), relocates,
    frees the RLD and ESD (`releaseheap(modend)`), allocates nothing
    more, and then calls the init function; ccrel's init calls
    `main`, so the whole program runs inside it. So the zeros and the
    RLD and ESD are in memory together while loading: `cccomp`'s
    module file is 66,964 bytes for a 52,272-byte program (the other
    14.7K is the header, RLD and ESD). The idea: `ccrel` places every
    global without an initializer (and every such `static` local)
    after the module image's end, at fixed addresses as now; the file
    holds only the data with values. The init function first
    allocates that block, checks that it got the module's end (it
    wouldn't if a reserved hi-res page made `allocheap()` skip ahead:
    then stop with an error), clears it (`memset`), and calls `main`.
    - Gains: the file shrinks by the zeros (about 16K for `cccomp`
      after the split, more for `ccrel`); the load peak drops by up to
      the RLD and ESD's size, since the block reuses their space; disk
      files shrink (E3); generated code is unchanged (`LAW`/`SAW` to
      fixed addresses); every ccish program gains.
    - Not changed: the memory needed once running. This answers "can't
      load", not "too big to run" (E1).
    - Costs: object format version 5 (a zero-filled size in `.syms`,
      symbols in that block, a relocation kind for it); about 20 bytes
      of init code per program.
    - First task: check plvm's loader (`src/vmsrc/plvm/cmdsys.c`),
      which every test uses, for the same order (free the RLD, then
      init, with nothing allocated between).
- **F7. More storage:** streams on a second drive, or an 800K 3.5" disk
  or a hard disk. No change to ccish, but not every Apple II has them.
  A RAM disk needs 128K, so it is not the 64K answer.
- **F8. Drop the message-text comments** (decided in discussion,
  2026-10-29; to be done later). Each message number in the source
  carries its text in a comment (`error(123 /* "redeclaration of" */,
  ...)`, C2), duplicating `ccish.msg`. The 275 such comments take
  about 12K of source (about 10K of parse.c's 136K, measured after the
  2-space reindent). They stay while the compiler is still changing a
  lot, since they make the code easier to read and change; they are
  removed only after the other major changes. Then
  `tests/check_msgs.sh` keeps its other checks (every number used is
  in the file, every number in the file is used, numbers unique and
  ascending) but no longer compares comments with the file.
- **F9. `ccpp` writes tokens, not text** (discussed 2026-10-30;
  promising, to decide in the `ccpp` design, M8, before it is
  written). A preprocessor splits the source into tokens anyway; if
  `ccpp` also turns keywords and operators into one-byte codes,
  converts numbers, and decodes escapes in strings and characters,
  `cccomp`'s lexer becomes a small reader of tokens. lex.c is 2,183
  bytes of code, 426 of data and 358 of strings; perhaps 1.5-2K leaves
  `cccomp`, which keeps the token names its messages use and the
  one-token lookahead (`next_is_rbracket()`). The `.i` gets smaller on
  disk too. Costs: it is no longer text, so `ccpp` needs a text output
  for debugging; line numbers become records in the token stream. It
  changes decision 2's "compact text `.i`".

View when listed (not a decision): E1 and E2 first, since they set the
target; then the biggest levers look like B1 (`Sym` split), B3 (final
pass as its own module), C2 (message text out of memory), and C1 (a
lean self-hosted build). A2/A3 are attractive but risky without
collision detection; A4 is the C89-sanctioned form.

#### 18.7 The back end as its own program, `ccgen` (designed 2026-10-30; step 1 done 2026-10-31)

Agreed in discussion (18.6 C1, "Direction after measuring", and D3;
decision 22): stages 2 to 4 (lowering, peephole, emitting) leave
`cccomp` for a program of their own, which reads the parser's IR from
a file. The user, on the reasons: the IR printer (`-i`) can come back
on the Apple II, in the program that reads the IR anyway, and the new
program has room for many more peephole rules. The file format is in
[OBJECT.md](OBJECT.md), "`NAME.ir`". Choices below are open for
review.

**Why.**
- `cccomp` is too big (18.6 C1: 52,272 bytes in the small build; its
  module file, 66,964, can't load on a 64K machine) and must still
  grow (`unsigned`). The three files that move are, measured with
  `tools/measure.py` (small build): `lower.c` 1,339 bytes of code and
  388 of data, `peep.c` 2,578 and 104, `emit.c` 2,197 and 3,092. The
  front end keeps a few hundred bytes of that (the IR buffer, the data
  writers, the summary), so the estimate is about 5K of code and 3K of
  data out of `cccomp`, plus `fn_code[]` (604 bytes), which only the
  back end needs. That alone doesn't make `cccomp` fit: 18.6 C1 step 2
  (`CCISH_CHECKS`) and F1 (splitting parse.c) are still wanted. They
  are decided after measuring the split.
- `ccgen` is small (about 6K of code and 4K of data, plus what is
  added below), so it has room for `-i`, for more peephole rules, and
  later perhaps for a window of a whole function instead of 10 ops.
- The `.ir` file is a stable interface: the back end can be tested on
  its own, and a native 6502 back end (4A) could be another program
  reading the same file.

**The pipeline.** `ccpp` (`.c` to `.i`, M8), `cccomp` (`.i` to `.ir`),
`ccgen` (`.ir` to `.obj` and `.syms`), `ccrel` (objects to the
module). On the Apple II the `CCISH` driver runs each with `modexec`
(B3a). The host's `ccish` runs all of them in one program, but through
the same files, so every test goes through the `.ir` format.

**Where the cut is.**
- `cccomp` keeps parse.c (with lex.c, name.c, type.c) and **the IR
  buffer** (`ir_put()`, `ir_here()`, `ir_holds()`, `ir_rotate()`,
  `ir_flush()`): the parser drives it to reorder code (4(d2)), so it
  can't move. Where `lower_all()` lowered the buffered ops, it now
  writes them to the `.ir` file. It also keeps **the data writers**
  (`data_byte()`, `data_word()`, `data_addr()`, `data_zero()`,
  `data_string()`, `data_offset()`): the parser calls them as it
  parses and uses the offsets they return. And it keeps what goes into
  the symbol summary (`func_sym()`, `data_sym()`).
- `ccgen` gets `lower()`, peep.c, and emit.c's building of functions
  (the function buffer, labels and fixup chains, frame sizes,
  `drop_frame()`, `F` records), plus a reader that copies the data
  records to `.obj` and writes `.syms`.
- Proposed files: `src/irout.c` (front end: the buffer and the writer,
  from lower.c, and the data records and summary, from emit.c),
  `src/irin.c` (back end: the reader, `-i`, `.syms`), `src/ccgen.c`
  (its `main`); lower.c keeps only lowering. Per-stage headers (C1
  note) can come with this or later.

**Questions investigated (2026-10-30).**
1. **The code offsets in `.syms`.** Could `ccrel` compute each
   function's offset from the `F` record lengths, so that the front
   end writes `.syms`? No: `ccrel`'s step 1 lays out the whole module
   from every `.syms` (code size and function offsets) before it reads
   any stream, so it would need a fifth read of every `.obj`, on a
   slow drive. So `ccgen` writes `.syms`. The front end puts the
   summary at the end of the `.ir` file (record `Y`), with the code
   size and the function offsets 0, and `ccgen` copies it, filling
   those in from its own `fn_code[]`. The `.obj` and `.syms` formats
   don't change, and neither does `ccrel`.
2. **Names.** `ccgen` has no name pool, but seven of emit.c's messages
   name the function (177 to 180 and 183 to 185, such as "function too
   large:"), and `-i` names the function at `FUNC` and at each `CALL`.
   So the front end writes a name record (`N`) for a function just
   before the first op that refers to it (its `FUNC` or a `CALL`),
   noted by a new `fn_flags` bit, `FN_NAMED` (32 is free). `ccgen` keeps
   the names: an offset per function and the name bytes. The bound,
   measured: parse.c has 278 function symbols with 2,982 bytes of
   names (counting a NUL each), so about 3.3K of pool plus 604 bytes of
   offsets; only functions the IR refers to get a record. (Cheaper, not
   chosen: keep only the current function's name, and have `-i` show a
   callee by number.)
3. **Source lines.** Diagnostics start with the source name and line
   (`src_name`, `src_line`), and `ccgen` has no lexer. So the file's
   header holds the source name, and a line record (`L`) before each
   function gives the line where it starts: `ccgen`'s errors read
   `x.c:120: error 177: function too large: f`. (Today such an error
   shows the line the parser had reached, near the function's end.)
4. **The data records.** The parser writes them while it parses, in
   the middle of a function's IR (string literals, `static` locals),
   and their offsets are fixed when written. They stay in the front
   end, and go into the `.ir` file exactly as in `.obj`; `ccgen` copies
   each one as it meets it. Only their order among themselves matters,
   as now, where a function's `S` records come before its `F` record.
5. **File size** (measured: `cccomp -i` on `tools/out/parse.c`, the
   self-compile input, which has 12,950 IR ops). The ops alone, by
   encoding:

   | Encoding | Bytes |
   |----------|-------|
   | tag, then each operand the op has as a word (a table per op) | 36,564 |
   | 2-bit form in the tag: none, byte, word, word and *f* | 27,787 |
   | **chosen:** tag with a 1-byte operand flag, prefixes for word operands and *f* | 28,177 |

   The whole file for parse.c is then about 45K: the ops, 6.9K of
   data records (parse.obj has `D` 6,009, `R` 240, `S` 638 bytes), the
   summary (about 5.6K, as parse.syms's 5,650), at most 3.8K of names
   and 0.5K of lines. parse.obj is 38,947 bytes, with 32,060 of them in
   `F` records.
6. **Disk space at once.** `cccomp` needs the `.i` and the `.ir`;
   `ccgen` the `.ir`, `.obj` and `.syms` (about 90K for parse.c).
   Today's compile pass needs the `.i`, `.obj` and `.syms`. Since the
   `.ir` is about the size of `.obj` and `.syms` together, the peak is
   about the same, provided the `.i` is removed once `cccomp` is done
   and the `.ir` once `ccgen` is (unless `-k`). E3's problem (parse.c's
   source) is unchanged.
7. **Time.** One more program load, and one more write and read of a
   file about the size of `.obj`. Not measured on a real drive (M13).
8. **Open files.** `cccomp` has the `.i` and the `.ir` open; `ccgen`
   the `.ir` and `.obj`, then the `.ir` and `.syms`. Two each, within
   the Apple II's 4 (and `ccish.msg` is opened only after an error has
   closed the others).

**The encoding** (in full in OBJECT.md). Records with a letter tag, as
in `.obj`; the `D`, `R` and `S` records are byte for byte the same, so
`ccgen` copies them whole. An IR op is its op number (below `$40`)
with bit 7 set if a one-byte operand *a* follows. A word operand
(outside 0 to 255) comes before the op in a `W` prefix, and the second
operand *f* in an `X` prefix; without them, *a* is 0 and *f* is -1.
Why this one:
- Neither the writer nor the reader needs a table of which op takes
  which operands: the writer picks the form from the values, and the
  reader fills in *a* and *f* without knowing the op. So `-i` can show
  any op, and a new op needs no format change.
- It is 23% smaller than words by table, and only 1.4% (390 bytes for
  parse.c) bigger than the 2-bit form, which would leave 64 tag values
  for the ops and the records together: there are 50 ops now, and
  `unsigned` (M11) would add several. Here ops have `$00`-`$3F` (and
  `$60`-`$7F` later if needed), and records the capital letters.
- *a* is 16 bits; a word is read sign-extended, so `CONST -1` stays -1
  on the host. Lowering uses constants as 16-bit values anyway
  (`mop_const()`).

**Extra information in the file, to make the programs simpler** (asked
for in discussion):
- a header with a magic number, a version, and the source file's name
  (for messages);
- `L` records: each function's first line (for messages; per
  statement later, for `-i` or debugging, if disk allows);
- `N` records: function names (for messages and `-i`);
- the `Y` record: the summary in `.syms`'s own layout, so `ccgen`
  copies it, filling in two kinds of field.

Considered and not added: the frame size in `FUNC` (not known until
the function ends; `ENDFUNC` has it), and a count of labels per
function (it would only save clearing the label table).

**`-i` in `ccgen`.** It prints each IR op as it reads it, before
lowering, in today's form (`IR CALL f nargs=2`), with names from the
`N` records, and an `L` record as `; line N`. The file holds the ops
in the order they were lowered before (after rotation), so the output
is today's, plus the line notes. `-i` is in both builds of `ccgen`
(the point is to have it on the Apple II), so it leaves
`CCISH_LISTING`, which then covers `ccrel`'s `-l` only (decision 21
changes to match). Its cost (`dump_ir()` and the op names) is small,
and in `ccgen`. `cccomp` loses `-i`; `ccish -i` still works.

**Removing files.** `cccomp` removes the `.ir` after an error (unless
`-k`). `ccgen` removes `.obj` and `.syms` after an error, and the
`.ir` after success, unless `-k`: the `.ir` is only an intermediate,
while `ccrel` keeps its objects because they can be linked again.
`ccish` removes all of them after success, unless `-k`, as now.

**Checks in `ccgen`.** "compiler stream file is damaged" (188) for a
wrong magic or version, an unknown tag, a prefix not followed by an
op, a label number of `MAX_LABELS` or more, a function number beyond
the summary's count, an op outside a function, a missing summary or
anything after it, and a function the summary says is defined that has
no `FUNC` (or the reverse).

**Testing.** Before the change, keep every test's `.obj`, `.syms` and
module from the current compiler (in the scratchpad); after it, all
must be byte for byte the same, in both builds: the split moves code
and should change no output. `run.sh` then runs `cccomp`, `ccgen` and
`ccrel`. New tests: a damaged `.ir` (cut short; wrong version), a
back-end error showing the file and line, and `-i`'s output for a
small file (the expected output written by hand from IR.md).

**Moving more work to `ccgen`** (re-reviewed in discussion,
2026-10-30). Work that only chooses how code is shaped can move
behind the `.ir` file as new IR ops. Agreed so far: A and C. Sizes
are from the self-compile listing (`tools/out/cccomp.lst`); savings
are estimates, to be measured.

- **A. Reordering moves to `ccgen`: `IR_SWAP`** (agreed). Today the
  parser reorders code in the 64-op IR buffer (4(d2)), and when the
  code has already left the buffer it falls back to temporaries, in
  three places (`finish_store()`, `i + p` in `binary()`,
  `copy_struct()`). Instead:
  - The parser emits `IR_SWAP` after the two blocks, with *a* = the
    length of the top block in IR ops and *f* = the length of the
    block below it. It still counts ops written (`ir_here()`, and
    `code_start()` for held constants), so the lengths are
    subtractions.
  - `ccgen` keeps the IR buffer (larger, sized by measurement). If both
    blocks are still in it, it exchanges them there, as `ir_rotate()`
    does now: no code at all. Otherwise it exchanges the values at run
    time, through temporaries of its own: slots past the parser's
    frame, whose offsets it fills in at the function's end, as it
    already does for the frame size (`drop_frame()` counts them).
  - *f* = -1 means "don't move code": a call in the right operand
    must stay before the load (compound assignment, C11 6.5.16.2p3;
    `compound_elem()`). Then `ccgen` rewrites the swap with the next
    op where it can (commutative ops drop it, `SUB` becomes `SUB NEG`,
    `LT` becomes `GT`, `LE` becomes `GE`, and the reverse), and uses
    temporaries otherwise (`/`, `%`, `<<`, `>>`): today's
    `reversed_op()` and `swapped_op()`, as peephole rules.
  - The block below leaves one value. The top block leaves one value,
    or, for a store whose value is used (`x = *p = e`), the value and
    its copy: the block then ends in `IR_DUP`, and the run-time
    exchange keeps both copies below the address.
  - The front end loses the buffer (316 bytes of code: `ir_put()`,
    `ir_rotate()`, `reverse()`, `lower_all()`, `ir_holds()`, ...; and
    384 of data), the three fallbacks, and `reversed_op()` and
    `swapped_op()` (237 bytes): about 0.6K of code and 0.4K of data.
    `ccgen` gains about 0.4K.
  - Later, a VM `SWAP` (issue 21) is a change to `ccgen` alone: the
    run-time exchange becomes `SWAP`, and a swap before a store can
    become the reversed stores `SWR`/`SBR`.
  - With A, the IR ops in the file are no longer in their final order:
    `-i` shows them as written, with the `SWAP`s.
- **C. Unsigned comparison ops: `IR_ULT`, `IR_UGT`, `IR_ULE`,
  `IR_UGE`** (agreed). `compare_pointers()` (167 bytes) flips both
  operands' sign bits itself today (`CW $8000 XOR` each side, issue
  21). Instead it emits the op, with *a* = the length of the right
  operand's block in IR ops, and `ccgen` inserts the flips: after the
  left operand (before the right block) and after the right one, or
  through a temporary if the right block has left the buffer. A
  constant operand is flipped at compile time (`CONST c` becomes
  `CONST c ^ $8000`). `==` and `!=` need no unsigned form. M11's
  `unsigned` uses the same ops for `unsigned int`, and VM opcodes
  `ISULT` and the rest (issue 21) would again be a change to `ccgen`
  alone.
- **B. Block copies and fills, as a hint on the call** (agreed as a
  conditional step: only if `cccomp` still needs space after A and C
  are measured). A plain `IR_COPY` with both addresses on the stack
  would lose today's word moves: with no `SWAP` or `OVER`, words can't
  be moved between two stacked addresses without a call, while the
  parser now recomputes each address from its variables per word
  (`a[i] = b[j]`). So instead the parser emits the call it would make
  anyway (`dst src CONST n CALL memcpy`, or `memset` for zeroing a
  local array), with a hint op just before it giving the two address
  blocks' lengths in IR ops, and marking them as safe to recompute
  (K_VAR and K_ELEM addresses: variables and constants, no calls or
  stores; *f* = -1 when not, as for an address a call returned).
  `ccgen` then compares the real encoded size of word moves (each
  block repeated with the word's offset added; a single local or
  global address becomes `LLW`/`SLW` and the like) against the call,
  and keeps the smaller. Ignoring the hint leaves a correct call, so
  the fallback costs nothing. The front end loses `copy_struct()`'s
  cost model and word-move loop (`addr_cost()`, `access_cost()`) and
  `init_zero()`'s choice, about 0.4-0.5K; `ccgen` gains about 0.3K, so
  the total hardly changes. Generated code improves little: parse.c
  has 14 `memcpy` calls for struct copies and no `memset`. A struct
  that a call returns (on the stack) is still stored by the parser.
  Needs A (the buffer and block lengths in `ccgen`).
- **Not moved:** constant folding (C needs integer constant
  expressions in the front end: array sizes, `case` labels, enum
  values, initializers); `parse_addr()` (address constants; C3 may
  simplify it); type checks and diagnostics (they need types and
  exact lines); the pending operands (K_VAR, K_ELEM: what to load at
  all), frame layout, labels, and the summary's type hashes. Also not
  worth it: `sort_cases()` (187 bytes; `ccgen` would need about 1K to
  buffer case tables, and error 140 would lose its line), and
  dropping unused labels in `ccgen` (it would need a whole function
  buffered).

**Steps.**
1. The split itself, with the same output: the two new source files
   and `ccgen.c`, the Makefile (`ccgen`, `ccgen-small`),
   `tools/measure.py` (a third program), `run.sh`, and the docs
   (README usage and layout, IR.md, OBJECT.md, this section's status).
   It stays a pure move, so that byte-identical output checks it.
2. Two clean-ups in `ccgen`, worth doing regardless of size (agreed),
   each with byte-identical output:
   - **Table-driven lowering:** one table of about 50 entries (each IR
     op's PLASMA opcode and kind: simple, comparison, branch, operand
     form) replaces `lower()`'s, `simple_op()`'s, `compare_op()`'s and
     `branch_op()`'s `switch`es (769 bytes; about 400 saved), leaving
     only the special cases (`FUNC`, `CONST`, `NOT`, the globals'
     fixups) in code. The IR-to-PLASMA mapping is then one table, as in
     IR.md's "Now:" lines, and a new op is one row.
     **Done** (2026-10-31): `lw_kind[]` and `lw_op[]`, parallel `char`
     arrays; the kinds are simple, comparison (then `NEG`), with
     operand *a*, global (*a* relocated as data; `DATAADDR` is one,
     as `LA`), pseudo-op (`$100` plus the table's byte: the pseudo-ops
     keep their numbers, since peep.c tests `op >= OP_FUNC_BEGIN`),
     and special: `FUNC`, `ENDFUNC`, `CONST`, `STRADDR`, `CALL`,
     `JOIN`, `RETURN` and `NOT`, in `lower_special()`. Measured:
     lower.c's code 1,429 to 1,013 bytes, data 100 to 200, so `ccgen`
     316 bytes smaller (17,657). Output byte-identical.
   - **Parallel arrays instead of `Mop`** in the peephole window
     (`win[]`) and of `Reloc` in emit.c (decision 15): each
     `win[i].field` now costs a `MUL` by 10 (24 `MUL`s in peep.c's and
     emit.c's compiled code; on the 6502 VM a software multiply), in
     the loop that runs for every op. The rewrite functions take window
     indexes instead of `Mop *`. Smaller (to be measured) and faster.
     **Done differently** (2026-10-31; open for review). Checking first
     showed that peep.c mostly reaches the window through `Mop *`
     pointers, which are cheap: 78 field accesses, 64 of them `->op`
     (`LLW a LW`, 3 bytes), which as a parallel array would be
     `LA w_op IDXLW i LW` (6 bytes): about 200 bytes more, against
     about 60 saved at the 31 multiplies, and slower. So instead:
     - `Mop` is 8 bytes, not 10: `b` is gone, and its two uses
       (`ENTER`'s parameter count, `OP_JOIN`'s second label) are in
       `fn`, which those ops don't otherwise use. Indexing the window
       is then `* 8`, which the peephole stage makes `CN 3 SHL`: the
       same size as `CN 10 MUL`, but a shift, not a multiply. All 31
       of peep.c's `MUL`s became `SHL`s.
     - emit.c's `Reloc` table, reached only by index, is parallel
       arrays, with the kind a `char`: its `MUL`s are gone.
     Measured: `ccgen` 17,657 to 17,490 bytes (emit.c's data 3,562 to
     3,434 and code 1,623 to 1,604; the window's data 100 to 80).
     Output byte-identical. The full conversion of the window stays an
     option, if speed ever matters more than those 200 bytes.

**Step 3, measured** (2026-10-31, after steps 1 and 2; small build,
`make measure`):

| Program | Code | Data | Strings | In all | Module file | Of it RLD and ESD |
|---------|------|------|---------|--------|-------------|-------------------|
| `cccomp` | 26,801 | 15,045 | 1,118 | 42,964 | 56,284 | 13,166 |
| `ccgen` | 8,063 | 8,798 | 629 | 17,490 | 21,222 | 3,578 |
| `ccrel` | 5,884 | 7,752 | 196 | 13,832 | 16,944 | 2,958 |

Before the split (541505c9), `cccomp` was 52,272 with a 66,964-byte
module file. `cccomp`'s data is almost all zeros (F6a would take them
out of the module file and the load-time peak). Not done: E1, which
needs a 64K Apple II emulator (none is installed here), so whether
56,284 bytes load, and 42,964 run, is still unknown. The decisions
this step was to inform (`CCISH_CHECKS`, F1, B) wait for E1, or for
the user.

**Step 4 (A, `IR_SWAP`) is done** (2026-10-31). Choices, open for
review:
- The parser writes ops straight to the file; `ir_here()` counts them,
  and `swap_blocks()` puts `IR_SWAP` (*a* the top block's length, *f*
  the block below's, -1 to keep code in place). The four uses:
  `finish_store()` (with `keep`, the `DUP` ends the top block),
  `i + p` in `binary()`, `copy_struct()` (source address first), and
  `compound_elem()` (*f* -1 when `e` has a call). Gone from the parser:
  its buffer, `ir_holds()`/`ir_rotate()`, the three temporary
  fallbacks and `reversed_op()`; `swapped_op()` stays (`c op x`).
- `parse_index()` now passes positions to `binary()`, so `i[p]` moves
  code too (it was a temporary). Where a start isn't known, the SWAP's
  lengths are 0 and -1.
- `ccgen`'s buffer (`src/irin.c`): 128 IR ops as parallel arrays; when
  full, the older half is lowered, so the last 64 are always held (the
  parser's buffer held at most 64, flushed at each expression's end).
  A SWAP done by moving code stays as a done placeholder: found while
  testing, an enclosing block's length (counted by the parser) would
  otherwise be one short, and the wrong code would move.
- A SWAP that can't move code waits for the next op, then: **free**
  (commutative ops; `SUB NEG`; `<` as `>`); **stash** (the value below
  saved in temporary 0 just before the still-held top block, and loaded
  after: one temporary, what `reversed_op()` and the old fallbacks
  did); or **exchange** (two temporaries; keeps the copy after a
  `DUP`). A first version always exchanged, which made `x /= f()` and
  `i + p` past the buffer bigger than before; the stash brought them
  back. The temporaries are `FRAME_TEMP(k)` operands, placed past the
  parser's frame at the function's end (emit.c), only as many as used;
  `MAX_TEMP_USES` 32, message 225.
- `-i` now prints at read time (irin.c), so it shows the SWAPs.
- Considered (the user asked): group markers (`STARTGROUP seq1
  NEXTGROUP seq2`) instead of lengths. Not chosen: the parser learns
  that a swap is needed only after writing both sequences, and the
  file can't take a marker afterward, so every possibly swapped
  sequence would need one; lengths carry the same information at no
  cost. A check that each moved block leaves one value (a stack-effect
  table in `ccgen`) would catch a wrong length; possible, not done.
- Output: the self-compile is byte-identical; three tests changed:
  `arrays` (`2[a]` now `LLA 0 CN 2 IDXW`, 1,458 to 1,453 bytes),
  `compound_call` (the two pointer cases with calls, 855 to 859 bytes;
  its comment updated), and `rotate_overflow`, rewritten: its 40-term
  sums now fit `ccgen`'s buffer, so they are 100 terms (199 IR ops) to
  still test the exchange and the stash (expected output by hand: 100
  each). 276 tests pass.
- Measured (small build): `cccomp` 42,964 to **41,985** (code 26,801
  to 26,210, data 15,045 to 14,657; module file 54,997); `ccgen`
  17,490 to 19,645 (the larger buffer, the three ways, the
  temporaries; more than the 0.4K estimated).

**Step 5 (C, unsigned comparisons) is done** (2026-10-31). Choices,
open for review:
- `IR_ULT`, `IR_UGT`, `IR_ULE`, `IR_UGE` (`$33`-`$36`, in the order of
  `LT GT LE GE`), *a* = the length of the right operand's code. The
  parser no longer flips sign bits (`flip_sign()` is gone, and
  `compare_pointers()` only checks types and pushes `r`); `binary()`
  writes the op for `<` `>` `<=` `>=` between pointers. The left
  operand of a pointer comparison is never a held constant, so the
  right operand's code is exactly what follows `mid`.
- `ccgen` flips `x` just before `y`'s code (a `PRE_FLIP` bit on its
  first op, beside the stash's bit) and `y` after it, or into `y` if it
  is one constant: the IR the parser wrote before, so the code is the
  same (all 98 test and self-compile inputs: only step 4's three test
  changes). If `y`'s code isn't held, both are flipped afterward, `y`
  saved in temporary 1 (a stash uses temporary 0, so they can nest).
  New test `unsigned_late.c` (a 199-op right operand; results by hand:
  `1 0 1`). 277 tests pass.
- Measured (small build): `cccomp` 41,985 to 41,949 (the parser's flips
  were small); `ccgen` 19,645 to 19,976. The value is for later: M11's
  `unsigned int` uses the same ops, and VM opcodes (`ISULT`, issue 21)
  would be a change to `ccgen` alone.
- Step 6 (B, the copy hint) stays conditional: `cccomp` is 41,949 bytes
  and its module file 54,929, but how far that is from fitting waits
  for E1.

**The block check is done** (2026-10-31; asked for in discussion, in
the full build only). `ccgen`'s lengths for `IR_SWAP` and the unsigned
comparisons come from the parser's count of the ops it wrote, and a
wrong count would move the wrong code with no message (as nearly
happened in step 4). Considered: the user's idea of group markers
(`STARTGROUP seq1 NEXTGROUP seq2`); not chosen, since the parser only
knows a swap is needed after writing both sequences (see step 4).
Instead, under `CCISH_CHECKS` (introduced here, decision 21's
`CCISH_CHECKS` step), `block_ok()` in irin.c walks each block before it
is moved, stashed around or flipped around: from each op's stack
effect (`stk_effect[]`), the block must leave the values its op says
(1, or 2 for a top block ending in `DUP`) and never use one from below
it, following branches through each label's depth. A failure is
"damaged" (188). A `CALL`'s effect needs to know whether its function
returns a value, so the name record's length byte carries that in bit
7 (IR version 2): no bytes in the file, and both builds write it, so
their files stay the same. The small build reads past it and checks
nothing.
- Verified: all 98 test and self-compile inputs pass the check, with
  objects unchanged. New test: a SWAP's length patched from 1 to 2 in
  the IR file is "damaged" with the full `ccgen` (the small one
  accepts it, as designed). 278 tests pass.
- Measured: small `cccomp` +16 bytes (41,965), small `ccgen` -4
  (19,972: one length check became redundant). Full `ccgen` +961
  (20,939): code +500 and data +461 (`fn_returns[]` 302, `lab_depth[]`
  104, `stk_effect[]` 55), more code than the 100-150 bytes estimated.
   - **Smaller path buffers in diag.c** (agreed, in every program; also
     in `cccomp` and `ccrel`): `msg_path[300]`, `stream_name()`'s
     `buf[300]` and `base_of()`'s `buf[256]` are 856 bytes of data
     sized for host paths. A ProDOS path is at most 64 characters (128
     with a prefix), so about 128 each, saving about 450 bytes in each
     program. A longer name is the existing "output name too long"
     error (1), or the like for the others; the tests' paths must stay
     within it, since the host runs the small build too.
     **Done** (2026-10-31): one limit, `MAX_PATH` 128 (`ccish.h`), for
     all three; a source name too long to make an output name from is
     now "output name too long" (1), not message 2. Doing it found a
     bug: an output name too long made `stream_name()` stop, and
     `clean_up()` then called it again to remove the files, endlessly
     (a crash). Now `remove_stream()` skips a name too long to exist,
     and `stop()` doesn't clean up a second time. New test: a 133-
     character IR file name. Measured: diag.c's data 876 to 405 bytes,
     code 871 to 900; each program 442 bytes smaller (`cccomp` 42,964,
     `ccgen` 17,973, `ccrel` 13,832). Output byte-identical.
   - **`char` for the parser's tables whose values fit** (decision 15;
     agreed, done ahead of the split where it checks out): see "Parser
     tables as `char`" below.
3. Measure and report `cccomp`'s and `ccgen`'s sizes; then decide
   `CCISH_CHECKS` (C1 step 2) and splitting parse.c (F1) with the new
   numbers. New peephole rules come after, one by one: measure, report,
   ask.
   - Then **E1** (18.6): measure the real free memory on a 64K PLASMA
     system, in an emulator. Without it, "is it enough?" (B, the
     front-end options below) can't be answered.
4. A: `IR_SWAP`, with the buffer in `ccgen`. Output should stay the
   same except where a fallback ran (a full buffer, rare).
5. C: the unsigned comparison ops (now, or at the start of M11).
6. B, conditional: only if `cccomp` still needs a few hundred bytes
   after step 5 is measured. If it fits, or is much further off,
   `CCISH_CHECKS` and F1 come first.

**Parser tables as `char`** (decision 15; done 2026-10-30, ahead of
the split, in commit e5ca9a1d). Each table was checked for the values it
holds:
- `fn_type[]` (return types) and `param_types[]` (parameter types):
  `char`. Both hold unqualified type handles only (indexes below
  `MAX_TYPES`, 56): the parser already drops a return type's
  qualifiers (`const int f(void)` returns an int, as in C17, which
  ignores them; gcc in C89 mode rejects `const int f(void);` followed
  by `int f(void) {...}`, in C17 mode accepts it), and parameter types
  are stored with `unqual()` or from `sig_type()`.
- `case_label[]`: `char`; labels are below `MAX_LABELS` (104).
- `label_uses[]` became `label_used[]`, a `char` flag: it was only
  ever tested for 0, so a count wasn't needed.
- Kept as `int`: `brk_label[]` and `cont_label[]` (`cont_label` holds
  -1 for "no loop", which a `char` would read back as 255 in ccish,
  whose `char` is unsigned; 64 bytes, not worth a sentinel change), and
  the tables whose values need 16 bits (offsets into the name pool and
  `param_types[]`, data and frame offsets, sizes, values).
- Since a host's `char` may be signed, `MAX_TYPES` and `MAX_LABELS`
  must now stay at most 128 (noted at both in `ccish.h`).
- Measured (small build): `cccomp` 52,272 to 51,372 bytes (data
  19,529 to 18,654, code 31,624 to 31,599); its module file 66,964 to
  66,052. Generated code is unchanged: 269 tests pass, and the
  self-compile inputs compile to byte-identical objects and symbol
  summaries with the old and new compiler.

**Step 1 (the split) is done** (2026-10-31). Choices, open for review:
- Files as proposed: `src/irout.c` (the IR buffer, moved from lower.c
  unchanged except that it writes instead of lowering; the data
  writers and the summary, moved from emit.c), `src/irin.c` (the
  reader), `src/ccgen.c` (its `main`). lower.c keeps lowering only,
  with `lower_op()` as its entry. One `ccish.h` still serves both
  programs (per-stage headers later).
- The line record holds `src_line` when `IR_FUNC` is put: the line of
  the function body's `{`. A back-end error reads, for example,
  `too_large.c:1: error 177: function too large: main`.
- `ccgen` keeps every function name the IR file names (`MAX_FNAMES`,
  3,281 bytes, 10% over parse.c's 2,982 for all its functions, a
  bound), and a full table is a new message, 224 "too many function
  names for ccgen". `func_name()` gives `?` for a function with no name
  record (only in a damaged file, which the reader rejects first).
- `-i` is in both builds of `ccgen`: `dump_ir()` now writes with
  `fputs()` and `num_text()`, not `printf()`, with the same output
  (the op name padded to 8). It prints as each op is lowered, which is
  still the order the ops were read; an `L` record prints as
  `; line N`. `cccomp` has no `-i` now. `opt_ir` is in every build
  (diag.c); `CCISH_LISTING` covers only `-l`.
- Files: `cccomp` removes the `.ir` after an error unless `-k`.
  `ccgen` removes its `.obj` and `.syms` after an error, unless `-k`,
  and keeps the `.ir` (for `-i`); after success it removes the `.ir`
  unless `-k`. `ccish` removes all three, as before (`remove_stream()`
  in diag.c removes one file). `ccgen output` takes the output name,
  as `ccrel` does.
- `fn_code[]` moved from parse.c to emit.c, set to -1 for "not
  written", so that the summary check (defined exactly when written)
  is direct.
- **ccrel's shared-name table grew:** every file still declares all of
  `ccish.h`'s functions, which the split added to, so linking ccish's
  programs needs 144 shared names with 1,297 name bytes (it was 131
  and 1,163), over `MAX_GNAMES` (1,281). `MAX_GSYMS` is now 163 (the
  first prime 10% above 144) and `MAX_GNAMES` 1,427 (10% above),
  about 300 bytes more in `ccrel`. The tests built on the limits
  changed with them (the full table: 163 names; a static name that
  doesn't fit: 43 long names; `tests/link/probe`'s colliding names,
  found again for 163 slots; two listings' last lines). Since imports
  are numbered in slot order, modules' ESDs changed order (accepted
  once before, for issue 11). Per-stage headers would bring this back
  down.
- Tests: `run.sh` compiles objects with `cccomp` then `ccgen`
  (`cc_obj`); new tests for `ccgen -k`, `-i`'s output (written by hand
  from IR.md), three damaged IR files (cut short, a wrong version,
  something after the summary: each reported, no stream files left,
  the IR file kept), and a back-end error's file and line. 275 pass.
  Before the change, every test program, link test file and
  self-compile input (98 files) was compiled with the old compiler;
  the new `cccomp` and `ccgen`, in both builds, write byte-identical
  objects and summaries for all of them.
- **Measured** (small build): `cccomp` 51,372 to **43,406** bytes
  (code 26,772, data 15,516, strings 1,118); its module file 66,052 to
  **56,706**. `ccgen` is 18,415 (code 8,469, data 9,317, strings 629;
  module file 22,163); its data is mostly the function buffer, the
  names (3.9K) and two file buffers. `ccrel` 14,274 (it was 13,966).

Still open outside this plan: the 6502 stack depth (18.5, "Stack and
static locals": the depth measurement, `MAX_NEST`, and nest counts in
`parse_unary()` and statements). It is a correctness problem on the
target, not a size one, and comes after this work.

**Front-end diet: options for later** (discussed 2026-10-30; not
decided, except that F9 is promising). Each keeps every feature:
- **F9 (C):** `ccpp` writes tokens, not text (in 18.6 F).
- **A6 (A):** identifiers as numbers (in 18.6 A).
- **C6 (C):** address constants from the expression parser, instead
  of `parse_addr()` (in 18.6 C).

Also: `ccgen`'s new tables (its IR buffer, the peephole window) are
sized from measurement, 10-20% above the peaks, like every other
table. Keeping only the current function's name unless `-i` (about
3.9K of data) stays an option, only if `ccgen` runs short.

**Open for review:** the name `ccgen` (others: `cclow`, `ccbe`); the
file names `irout.c` and `irin.c`; `ccgen` removing the `.ir`; line
records only per function; `ccgen` keeping every name (about 3.9K) or
only the current function's; A's run-time exchange for a top block
that ends in `IR_DUP`; the size of `ccgen`'s IR buffer.

### 19. Target application: a small Unix-like command set (possible, not committed)

Once the compiler works well, use it to build a small toybox-style
set of commands, giving a simple Unix-like interface on a 6502. This is
a goal, not a commitment, and it doesn't change the current milestones.
It does suggest which way to lean on a few choices.

**Why it fits:**
- **One module per command.** PLASMA already loads modules by name,
  so `cat`, `ls`, `cp` and the rest can each be a small module loaded
  from disk when run. A shared C-ish runtime module stays resident
  between commands. On 64K this beats one large BusyBox-style binary.
- **ProDOS has a hierarchical file system.** `ls`, `cd`, `pwd`,
  `mkdir`, `rm`, `mv`, `cp`, `cat`, `head`, `tail`, `wc`, `grep`, `od`,
  `echo` and `more` map onto it cleanly.
- **Text tools are mostly disk-bound**, so bytecode speed matters less.
- **It's a realistic test corpus** for the compiler: real programs
  will find bugs and show where the code is too big.

**Multitasking is feasible.** An earlier draft of this issue said
there would be no multitasking; that was wrong. The first Unix ran on
a PDP-7 with 8K 18-bit words, and First Edition on a PDP-11/20 with
about 24K bytes. Both had processes and `fork`, and neither had memory
protection. Multitasking 6502 systems exist too: LUnix and GeckOS on
the C64, and Contiki's cooperative tasks. PLASMA itself already
provides cooperative threads in `../src/libsrc/fiber.pla`. Its
`fbrSwap` saves and restores the VM's zero-page evaluation stack,
frame pointer and 6502 hardware stack. Because all our programs are
bytecode, a task switch only has to save that small amount of VM
state. Options:

- **A. Several processes in memory, switching only when one waits.**
  A switch happens when a process blocks: a pipe is empty or full, it
  reads the keyboard, or it calls `wait`. This gives real pipes
  (`ls | grep x`) and background jobs.
  - Pros: builds directly on the fiber design. No timer is needed.
  - Cons: a process in a compute loop never gives up the CPU unless
    the runtime adds yields. Memory limits how many processes fit,
    probably 2 to 4 small ones on 64K.
- **B. First Edition-style swapping.** Only one process is in memory;
  the others are swapped out.
  - Pros: each process gets nearly all of memory, and `fork` is easy
    (write out a copy of the image).
  - Cons: far too slow on a floppy. Reasonable only with a RAM disk,
    128K auxiliary memory or a RamWorks card.
- **C. Preemptive time slicing.**
  - Pros: true time sharing.
  - Cons: a stock Apple II has no timer interrupt; it needs a mouse
    card, a clock card or the IIc vertical-blank interrupt. The VM
    could check a flag between bytecodes, so switches happen only at
    safe points. ProDOS can't be re-entered, so file calls must also
    be serialized.

Suggest A first, B as an option for machines with extra RAM, and C as
a stretch goal (possible, not committed).

**Limits (it's Unix-like, not Unix):**
- No memory protection: a buggy program can crash everything, as on
  early Unix. No users or permissions.
- ProDOS limits show through: names of up to 15 characters, case
  insensitivity, ProDOS file types, and at most 8 open files.
- Toybox's own source likely won't compile unchanged. It uses a heavy
  preprocessor (generated option tables, macro tricks), 32-bit sizes,
  `mmap` and much of POSIX. The realistic path is to write small
  C-ish versions of the commands, borrowing toybox's design and simpler
  code where useful (toybox is 0BSD-licensed, so that is allowed).

**What it would need from ccish:**
- **M9 runtime library shaped like POSIX**, rather than like PLASMA:
  `open`, `read`, `write`, `close`, `lseek`, `opendir`/`readdir`,
  `malloc`, and `argc`/`argv` from the command line, over ProDOS.
- **From M11:** `unsigned`, varargs `printf`, and function pointers
  (for option tables and dispatch).
- **Some 32-bit support.** ProDOS files can exceed 64K, so `ls -l` and
  `wc` need it. Options: a `long` subset, or calls into PLASMA's
  `int32` library. This may argue for moving limited `long` support
  earlier than "stretch goal".
- **A small kernel layer** in the runtime: a process table, pipe
  buffers, and blocking `read`/`write` that switch tasks.
- **A small shell**: command lookup, redirection, pipes, background
  jobs, simple variables and scripts.

### 20. Writing fast C-ish

Guidance for programmers (from discussion, 2026-10-27), based on the
6502 VM's code (`src/vmsrc/apple/plvm02.s`). It belongs with the hints
of issue 10, and may become part of the user documentation.

- **Prefer unsigned `char` and signed `int`.** Byte loads (`LB`,
  `LLB`, `LAB`) zero-extend, so an unsigned `char` costs nothing, and a
  signed `char` would need sign extension on every load (a call to
  CMDSYS `sext`, or inline code). That is why plain `char` is unsigned
  (issue 7). PLASMA's ordering comparisons, `DIV`, `MOD` and `SHR` are
  signed, so a signed `int` is free, and `unsigned int` (M11) will cost
  extra only for `< > <= >=`, `/`, `%` and `>>`. `+ - *`, `==` and `!=`
  work the same for both.
- **Test pointers with `==` and `!=`, not `< > <= >=`.** Ordering
  comparisons of pointers are unsigned, so each side gets `CW $8000
  XOR` (4 bytes and 2 ops per side; a constant side is folded).
  Equality costs nothing extra. So write `while (p != end)`, not
  `while (p < end)`.
- **A `char` index is a little faster than an `int` index.** Reading a
  byte local skips the high byte: `LLB` is about 5 cycles faster than
  `LLW`, and the fused `ADDLB` about 12 faster than `ADDLW`, against
  roughly 15–20 cycles of dispatch per op. (ccish emits `ADDLB`,
  `IDXLB` and the other fused ops only from M10, issue 17.) But a
  `char` index limits an array to 256 elements and wraps: `for (char
  i = 0; i < 256; i++)` never ends.
- **For a simple scan, walk a pointer; otherwise, consider an index.**
  Reading through a pointer is `*p` = `LLW p LB` (2 ops), while
  `buf[i]` is `LA buf ADDLB i LB` (3 ops, with M10's fused op), and
  `p++` and `i++` are both 3 ops. An index wins when the loop counter
  is needed anyway (one variable instead of two), when several arrays
  share it, when the loop test can be `i < n` (a plain signed compare)
  instead of `p < end`, and for random access.
- **Multiplying by a constant** is cheap for 0, 1, 2, 3 and powers of
  two (`DUP ADD`, `DUP IDXW`, `CN n SHL`); other constants use `MUL`,
  a 16-step loop. Dividing by a constant always uses `DIV` for signed
  values (issue 17 and the M4 step 2 notes).

### 21. New VM opcodes for C (possible, not committed)

Raised in discussion (2026-10-27): the VM might be changed after all
(4(e) said to measure before considering SWAP again). Opcodes `$C6` to
`$FE` are free: 29 slots (the 6502 VM's `OPTBL` ends at `JUMP`, `$C4`).

**Cost of any new opcode:**
- It must be added to every implementation: 7 VMs (`plvm01.s`,
  `plvm02.s`, `plvm03.s`, `plvm802.s`, `plvmc64.s`, the C VM `plvm.c`,
  and `plvmjit02.s`/`plvmjit03.s`) and the JIT compilers
  (`jitcore.pla`, `jit16core.pla`, `cleanjit.pla`).
- Modules that use it won't run on existing PLASMA installs, and an old
  VM doesn't reject an unknown opcode: it dispatches through memory
  past its table and crashes. So ccish should emit new opcodes only
  when asked (a target option), and such a module could list a marker
  module (say `CCVM`) as a dependency, so that an old system fails to
  load it cleanly (`?CCVM`) instead of crashing.
- Ideally the change is accepted upstream, so ccish programs run on
  standard PLASMA.

**Candidates, most useful first:**
1. **Reversed stores, `SWR` and `SBR`: `( addr val -- )`.** C computes
   a store's address before its value, and these take them in that
   order. Every store through a computed address, including
   `*d++ = *s++` and `a[i++] = x`, would need no temporary and no
   SWAP: 0 extra bytes, against 4 for a spill and 1 for SWAP. That
   also makes M10's block exchange unnecessary for stores. Two
   opcodes.
2. **Unsigned comparisons, `ISULT`, `ISUGT`, `ISULE` and `ISUGE`.**
   They would replace the sign flips on pointer `< > <= >=` (8 bytes
   and 4 ops per comparison, or 4 bytes with a constant side) and
   serve M11's `unsigned int`. They would give `$FFFF` for true, like
   PLASMA's own comparisons. Four opcodes. (CMDSYS's `isult` and
   friends do this today, at the cost of a call.)
3. **`SWAP`: `( a b -- b a )`.** General, 1 byte. One opcode (it was
   `$2E` until 2015). It would make ccish simpler, not just its output
   smaller (noted during M4 step 3):
   - Any store could compute its address first, as C reads it:
     `addr e SWAP SW`. That costs 1 byte and no temporary, against 4
     bytes and 2 frame bytes for a spill.
   - Compound assignment would compute the address once, with no
     temporary: `addr DUP LW e ADD SWAP SW`. That also fixes the
     mismatch with C in `a[i] += f()` (see the M4 step 3 notes).
   - Most of the cost it removes is in the compiler. M10's block
     exchange (the `MARK`/`SWAP2` pseudo-ops of 4(d)) would no longer
     be needed for stores, and neither would the spill paths in
     `parse.c`. Pending places, and the lookahead that keeps `a[i]`
     pending, would become an optional size optimization:
     `e LLA a LLW i IDXW SW` is still 1 byte shorter than
     `LLA a LLW i IDXW e SWAP SW`.

   Reversed stores (candidate 1) save that last byte on stores, but
   SWAP alone covers stores, compound assignment and other reorderings
   (such as `i + p` scaling) with one opcode. It is also the easiest to
   argue for upstream, since it is a general stack operation that
   PLASMA once had.

   (Update, 2026-10-29: rotation in place, 4(d2), now comes first, in
   M5 step 4. It covers the stores and `i + p` with no opcode, so what
   SWAP would still add is mainly compound assignment and `++`/`--`
   on a computed address, which need the address twice.)
4. **For M11's `unsigned`:** `UDIV` and `UMOD` (otherwise a runtime
   routine), and a logical shift right `USHR` (otherwise `SHR` then a
   mask, which needs a computed mask for a variable count). Three
   opcodes.

**Not worth an opcode, for now:** sign extension of a byte (avoid
`signed char`, issue 20), fused compare-and-branch for `<` (saves 1
byte), and loads at an offset for struct members (decide after M6, by
measuring).

**How to decide:** count spills and sign flips in real code, such as
the compiler's own source at M12, compiled both ways. If VM changes are
on the table anyway, fixing the C VM's `NOT` and `DIVMOD` discrepancies
(`doc/Bytecodes.md`) would be worth doing at the same time.

## Decisions so far

The first round of answers is recorded inline as "Answer:" under each
issue. In summary:

1. **Self-host on a 64K Apple II**, as one program (no multi-pass).
   Fit it in through careful design and by dropping hard features
   (issues 1, 2). Since revised: memory forced separate programs run
   one at a time (18.6 B3, and decision 2).
2. **Preprocessor:** none at first; then the minimal one (3B), as a
   separate program that writes a compact `.i` file (no comments, few
   spaces, `#line` to keep line numbers), because of memory limits
   (revised 2026-10-29; issues 2, 3). So a build is three programs:
   preprocessor, `cccomp`, `ccrel`.
3. **Architecture:** the four-stage op-stream pipeline (4A), with
   stages as push-style functions in one program.
4. **No SWAP opcode.** Use spills first, then add block exchange as an
   optimization. Reconsider SWAP only if measurements justify it
   (4(e)). Issue 21 has since reopened this: SWAP would simplify the
   compiler as well. Block exchange comes first, as rotation in place
   in M5 step 4 (4(d2)). It removes the spills that only fix order;
   the spills that reuse an address stay.
5. **Expressions:** precedence climbing (5A).
6. **Varargs:** caller-built argument block plus count (6A), but not in
   the first milestone.
7. **String literals:** `[len][chars][NUL]`, with the C pointer at the
   first character (issue 8).
8. **Digraphs yes, trigraphs no** (issue 14).
9. **Grow the compiler in small end-to-end milestones.** Self-hosting
   is the eventual goal, not the first milestone. Milestone 1 handles
   only a handful of constructs, but goes all the way from C source to
   a REL module that runs under `plvm`. Each later milestone adds a
   little and keeps everything working. See "Milestones" below.
10. **Wanted in the language, at different milestones:**
    - `struct` (M6), `switch` and simple initializers. Simple
      initializers are needed for real-world use. "Simple" means:
      - scalars and pointers (`int n = 5;`, `char *s = "hi";`,
        `int *p = &x;`);
      - `char` arrays from string literals;
      - one-level brace lists (`int t[] = {1, 2, 3};`).
    - Then `enum` and `typedef` (after M7), and later `unsigned`,
      varargs, function pointers and nested initializers.
    - Later still, if a program needs it: `union` (decision 17).

    Implementation notes on initializers:
    - Global initializers are folded at compile time.
    - Address constants in the data segment need relocation (RLD)
      entries.
    - Local initializers compile as ordinary assignments.
11. **Multiple returns:** a returned struct of up to 8 bytes is a
    PLASMA multi-value return, one value per word (issue 16; changed in
    M6 step 3 from "4 scalar members"). A larger struct can't be passed
    or returned by value (decision 16).
12. **Streamed output, a separate final pass, and fixup chains**
    (issue 18.1 and 18.2):
    - The compiler writes a data stream, a code stream (each function's
      code followed by its relocation records), and a symbol summary.
    - A separate final pass, which doubles as a linker, writes the REL
      module.
    - Forward branches use fixup chains within a function buffer.
    - All file I/O uses 512-byte block buffers.
13. **M4 types** (see "M4 design" under Milestones): a table of type
    entries; strict pointer type checking, with casts; unsigned
    pointer comparisons; zeroed global arrays in the data segment for
    now.
14. **M5** (see "M5 design" under Milestones): address constants
    through a small separate parser; string literals in their own
    pool; local array initializers as element stores; `static` on
    globals, functions and locals; `const` enforced, with warnings.
15. **Space first, in the compiler's own code and data** (decided in
    discussion, 2026-10-29). The compiler must eventually fit a 64K
    Apple II, compiled by itself, so every design is checked for the
    space its data structures and its code take there:
    - **Use the smallest type that holds the values:** a `char` (0 to
      255) wherever it will do, an `int` only where needed, and pack
      flags and small fields into bits when that saves real space.
    - **Use parallel arrays, not arrays of structs:** one array per
      field, all of the same length (one constant), with comments
      tying them together. In code ccish compiles, reading
      `type_sub[t]` is `LA type_sub LLW t IDXW LW` (5 bytes), but
      `types[t].sub` with 6-byte entries needs `t * 6 + 2` first
      (about 9 bytes, with a multiply or `DUP IDXW DUP ADD`). A `char`
      array is `... ADD LB`. It also needs no `struct`, which ccish
      lacks until M6.
    - **Applied so far:** the type table (M5 step 6). **Next:** `Sym`,
      an 11-`int` struct shared by functions and variables, most of
      whose fields variables don't use. Splitting it into function and
      variable tables of parallel arrays is planned as its own step
      after M5 step 6. M12 (issue 18.4) then measures what is left.
16. **Generally compatible with C; a subset by design** (decided in
    discussion, 2026-10-29). ccish can't reasonably implement all of C
    on a 64K machine, so:
    - **What ccish supports generally behaves as C says** (C89, with
      some C99), so the same source compiles with gcc and means the
      same thing. Where C leaves a choice to the implementation, ccish
      makes one and documents it: `int` is 16 bits, `char` is unsigned,
      `>>` of a negative value is arithmetic.
    - **Some of C is left out on purpose:** parts that are hard to
      implement, or costly in space, and not needed to self-host the
      compiler. Examples: floating point, `long`, bitfields, and structs
      over 8 bytes passed or returned by value. A supported feature may
      also be limited the same way (planned: `unsigned` `/` and `%`
      only by powers of two). Using a left-out feature is a
      compile-time error ("not supported yet" for one planned for a
      later milestone, or a message naming the limit).
    - **Strict compatibility may have to give way so the compiler
      fits.** When it does, the goal is incompatibilities that are
      documented and unlikely to affect real code, and that are caught
      at compile time where that is cheap.
    - **Known differences:** a hex constant that doesn't fit in `int`,
      such as `0x8000`, is the `int` -32768, while in C it is
      `unsigned` (to be fixed when `unsigned` arrives, M11).
    - The CMDSYS functions that programs call today (`puts`, `memset`,
      ...) keep PLASMA's meanings; they are library names, not the
      language, and are replaced by a C library in M9 (issue 12).
17. **What self-hosting needs comes first; goodies come later**
    (decided in discussion, 2026-10-29). The concern is that even
    optimized, the compiler may not fit in 64K, so each feature's cost
    in the self-hosted compiler counts. Build the features ccish's own
    source needs first (the census in 18.5), then fit it (M12), then
    add features it doesn't need ("goodies", such as `union`) only as
    room and real programs allow. A feature that is cheap to add is
    still not free: it costs compiler code and tables on the Apple II.
18. **One combined stream file (18.6 B6a)** (decided in discussion,
    2026-10-29; done the same day, object format version 3). PLASMA's
    Apple II file library allows at most 4 open files, and the compile
    pass kept 5 open, so the four stream files became one stream of
    tagged records, `.obj`, with `.syms` still separate and written
    last. Disk space stays about the same (18.6 E3).
19. **C source style: 2-space indents, no tabs** (decided in discussion,
    2026-10-29). Tabs show as different widths in different tools.
    All the C files (sources and tests) were converted from 4 spaces
    once, by a script not kept: statement lines' indents were halved, and continuation
    lines moved with their statement's first line, so alignment under
    a parenthesis or operator is kept. parse.c went from 145,133 to
    135,813 bytes (18.6 E3's table predates this).
20. **Separate compilation, statically linked by `ccrel`** (decided in
    discussion, 2026-10-30; issue 11). Functions must be declared
    before they are called (no implicit declarations); `extern` data
    and functions are shared between files through object format 4
    (data symbols, `static` kept local, a type hash); `ccrel` links any
    number of objects into one module, later also into one object, and
    later from archives.
21. **Two builds from one source: full and small** (decided in
    discussion, 2026-10-30; 18.6 C1, "Two builds: the plan"). The full
    build (the default; Linux, the tests) keeps everything; the small
    build (`-DCCISH_SMALL`, for the 64K Apple II) drops `-i`, `-l` and
    the diagnostics a valid program never triggers. Both have the same
    limits, and generate the same code for valid programs. The user:
    "Loss of error-detection is sad, but never working on correct code
    is worse." Changed by decision 22: `ccgen` keeps `-i` in both
    builds.
22. **The back end is a program of its own, `ccgen`** (decided in
    discussion, 2026-10-30; design in 18.7). `cccomp` parses and
    writes the IR to a `.ir` file (OBJECT.md); `ccgen` lowers,
    optimizes and emits it as `.obj` and `.syms`, which `ccrel` links
    as before. `ccgen` prints the IR (`-i`), on the Apple II too, and
    has room for more peephole rules.
23. **`unsigned`, with helper functions merged by the linker**
    (decided in discussion, 2026-10-31; "`unsigned` and helper
    functions design" under Milestones). `unsigned` arithmetic that
    PLASMA lacks is new IR ops, which `ccgen` lowers inline or as calls
    to helper functions it writes into each object that needs them;
    `ccrel` keeps one copy of each. `signed char` is not supported yet.


## Milestones

**`unsigned` and helper functions design (decided in discussion,
2026-10-31; pulled forward from M11, since ccish's own source uses
`unsigned` about 60 times, including `/` and `%` by variables in
`fold()`).** The user: "We know it won't load as-is. We also know we
need a preprocessor, as well as support for unsigned." `unsigned`
first: it is contained, self-hosting needs it, and it doesn't depend on
the preprocessor's design (F9).

*The language.*
- New basic type `TY_UINT` (16 bits): `unsigned`, `unsigned int`,
  `int unsigned`. `unsigned char` is `char` (ccish's plain `char` is
  already unsigned, issue 7; ccish doesn't tell the two apart, a
  documented difference). `signed int` and `signed` are `int`;
  `signed char` is "not supported yet" (it needs sign extension).
  `short` and `long` stay unsupported. The type hash's letter for it
  is `u`.
- C's usual arithmetic conversions: `char` promotes to `int`; `int`
  meeting `unsigned` in a binary operator becomes `unsigned`; the
  result of `== != < > <= >=` is `int`. Assignment, arguments and
  returns convert silently, as C does (the same bits). Casts retype.
- Constants: a `u` or `U` suffix makes one `unsigned`; an unsuffixed
  hex or octal constant that doesn't fit in `int` (`0x8000` and up) is
  `unsigned`, as C says, which removes a documented difference. A
  decimal constant over 32767 stays the known difference (it would be
  `long`). `sizeof` is `unsigned` (`size_t`), as in C.
- Constant folding follows the operands' type: unsigned `/`, `%`,
  `>>` and comparisons when either side is `unsigned`.
- No warning for mixing signed and unsigned yet (gcc's
  `-Wsign-compare`); possible later.

*The IR.* `+ - * & | ^ << ~ == !=` are the same bits as for `int`.
`< > <= >=` are `IR_ULT` and the rest (18.7, C). New ops `IR_UDIV`,
`IR_UMOD` and `IR_USHR` (`$37`-`$39`), binary, `( x y -- x op y )`,
which the parser writes for unsigned `/`, `%` and `>>` (and `/=`, `%=`,
`>>=`). `ccgen` lowers `USHR` by a constant inline (`SHR n` then a
mask), and the rest as calls to helper functions; VM opcodes for them
(issue 21) would be a change to `ccgen` alone.

*Helper functions.* Code that `ccgen` supplies, called like a
function, written into each object that uses it, of which `ccrel`
keeps one copy (like a linker's COMDAT sections). The front end never
names them; the IR ops are the interface.
- Helpers are numbered (0 udiv, 1 umod, 2 ushr). A call to one is a
  `CALL` with a new relocation kind, `REL_HELPER` (5), whose target is
  the helper's number: not a reserved function number, since function
  numbers are positions in `.syms`, checked against its count, and a
  "negative" 16-bit index would need special cases in every check.
- Each helper is a short table of IR ops in `ccgen`, lowered through
  its own stages like a function; a helper is a leaf with no
  relocations. Unsigned division uses the signed `DIV`, without
  branches: q = ((n >> 1) / d) << 1 (logical shift), then one more if
  the remainder is still at least d, with d >= $8000 as its own case
  (q is then n >= d).
- Object format version 5: a new stream record `H` (a helper's number,
  then its bytecode), written once per object for each helper it uses,
  after its functions; and in `.syms`, a list of the helpers the
  object defines: number, size, a 16-bit checksum of the bytes, and a
  name for the listing (`udiv` ...). `ccrel` needs the sizes in its
  first step, before it reads any stream.
- `ccrel` keeps the first object's copy of each helper, places it once
  after every object's code (before init), resolves `REL_HELPER` to it,
  and gives it an RLD function entry. Two objects whose copies differ
  in size or checksum (different `ccgen`s) are an error, not a silent
  merge. `-l` lists a helper by its name.
- Considered: `ccrel` holding the helpers' code itself (nothing
  duplicated, simpler formats). Not chosen: two programs would then
  have to agree on each helper's calling convention, and no module
  could be built without `ccrel`'s copies; this way `ccgen` alone knows
  them, for a few dozen bytes of disk per object that uses one.

*Steps*, each tested and committed (the order of the first two changed
from the discussion: helpers are tested best from C programs that need
them):
1. `unsigned` in the parser: types, conversions, constants, folding,
   `sizeof`, the new IR ops, `USHR` by a constant inline. Unsigned `/`,
   `%` and `>>` by a variable are "not supported yet" until step 2.
2. The helper system: format version 5, `REL_HELPER`, `H` records, the
   helpers, merging and checks in `ccrel`, the listing; then unsigned
   `/`, `%` and `>>` everywhere. Tests: programs that divide, a link of
   two objects that both use a helper (one copy kept), and a mismatch.
3. `tools/measure.py` stops rewriting `unsigned` as `int` and stops
   dropping number suffixes, so the self-compile measures what
   `unsigned` really costs in ccish's own code.

Estimated: `cccomp` +0.5-0.8K, `ccgen` and `ccrel` +0.3-0.5K each; to
be measured.

**Step 1 (`unsigned` in the parser) is done** (2026-10-31). Choices,
open for review:
- `TY_UINT` (index 3, kind `TK_UINT`), hash letter `u`. Keywords
  `signed` and `unsigned` (`T_SIGNED`, `T_UNSIGNED`); `parse_int_type()`
  takes `int`, `char`, `signed`, `unsigned` in any order, with
  qualifiers among them; a repeat or conflict is message 226, `signed
  char` 227.
- The lexer sets `tok_unsigned` for a `u`/`U` suffix, or a hex or octal
  constant over `$7FFF`; message 11's text now says "number suffixes
  other than u". `tests/run/compare.c`'s `0x8000 < 0` (written for the
  old difference) became `(int)0x8000 < 0`.
- `is_uns()`, `arith_type()` (C's usual arithmetic conversions) and
  `promoted()` (unary operators, a shift's left operand) give result
  types; `?:` of different integers uses `arith_type()`; `sizeof` is
  `unsigned`. `fold()` takes an `uns` flag: unsigned `/ % >>` and
  comparisons.
- `unsigned_op()` in `binary()`: `< > <= >=` with an unsigned side are
  `IR_ULT` and the rest (the right operand's length; 0 when the left
  is a constant, which is written at the right operand's start, so
  `ccgen` flips afterward); `/ % >>` are `IR_UDIV`, `IR_UMOD`, `IR_USHR`
  (`$37`-`$39`), with *a* = 1 when the right operand is one constant
  (the parser says so, rather than `ccgen` guessing from the op before).
  The `c op x` path writes the unsigned comparison with length 1.
- `ccgen` (irin.c, `unsigned_const()`) does those by a constant inline,
  rewriting held ops without changing their number: `USHR c` is `SHR`
  and a mask (lower.c); `UDIV 2^n` makes the constant n and becomes
  `USHR`; `UMOD 2^n` makes it 2^n - 1 and becomes `AND`. Anything else
  is message 228 (in the parser) until step 2; `ccgen` reports one
  without a constant as damaged.
- Tests: `unsigned.c` (every spelling, conversions, comparisons, the
  inline `>> / %`, folding, `sizeof`, compound assignment, a call, `?:`;
  results by hand, large values printed in hex), and errors
  `signed_char.c`, `type_specifier_twice.c`, `unsigned_div_var.c` (the
  last goes away in step 2). 282 tests pass.
- Generated code: all test objects are as before except `compare.c`
  (its source changed); the self-compile's diag, lex, link and lower
  objects changed, since `sizeof` and hex constants over `$7FFF` are
  now `unsigned`, as in C (`strlen(...) >= sizeof(buf)` is now an
  unsigned comparison, `0xFFFF >> a` a logical shift).
- Measured (small build): `cccomp` 41,965 to 43,042 (parse.c's code
  +984: more than the 0.5-0.8K estimated). `ccgen` can't be measured
  until step 2: lower.c's `0xFFFF >> a` is now an unsigned shift by a
  variable, which step 1 doesn't do.

**Step 2 (helper functions) is done** (2026-10-31). Choices, open for
review:
- `src/helper.c` holds udiv, umod and ushr as IR tables (parallel arrays
  of op and operand), lowered by `gen_helpers()` just before the
  summary, as function numbers `HELPER_FN(k)` (`MAX_FUNCS + k`), for
  each helper the object calls (`helper_used`). Unsigned comparisons in
  them are written out with their sign flips, since they are lowered
  directly, not through irin.c's buffer. udiv: d >= $8000 gives n >= d;
  else q = ((n >> 1) / d) << 1, plus 1 if n - q * d >= d. umod the same
  for the remainder; ushr shifts once logically, then arithmetically.
- `lower.c`: `UDIV` and `UMOD`, and `USHR` without a constant count, are
  a `CALL` with `FIX_HELPER`; irin.c now passes a constant count in *f*
  (so a count of 0 is told from none). The parser marks a constant
  (*a* = 1) only when it can be done inline (a shift; a power of 2 for
  `/` and `%`); message 228 and its test are gone.
- `emit.c` writes a helper as an `H` record, with no relocations and not
  in the code size, and keeps its size and checksum (`h * 31 + byte`, as
  the type hash); irin.c lists the helpers at the summary's end.
  Object format 5.
- `ccrel`: `close_syms()` reads the helper list in every pass, and in
  step 1 notes the first object holding each and checks the others
  (message 229, with the helper's name); the helpers go after every
  object's code, before init; `REL_HELPER` resolves to that copy;
  `copy_helpers()` copies each from its object's stream; each gets an
  RLD function entry and counts in the header's function count; `-l`
  lists `helper udiv:` and notes `; helper udiv` at calls.
- The self-compile found a comma operator (which ccish doesn't support)
  in my first irin.c; it uses `strlen()` now.
- Tests: `unsigned_div.c` (by variables, divisors of $8000 and more, 1,
  shifts by 0 to 15, compound assignment, decimal printing through the
  helpers; results by hand); `tests/link/helpers` (two objects that both
  call udiv: one copy in the module, both calls to it); a mismatch (one
  object's checksum changed: 229). 284 tests pass. Objects for all 98
  saved inputs are unchanged (only `.syms`: version 5 and the count).
- Measured (small build): `cccomp` 43,053 (+11 from step 1); `ccgen`
  21,210 (19,972 before `unsigned`: step 1's lowering and the helpers,
  155 bytes of code and 336 of tables); `ccrel` 14,520 (+667, more than
  the 0.3-0.5K estimated).

**Step 3 (measuring real `unsigned`) is done, so the design is done**
(2026-10-31). `tools/measure.py` no longer rewrites `unsigned` as
`int` or drops `u` suffixes (it still makes `long` and `short` `int`,
and drops `l` suffixes). Every file compiles. What `unsigned` costs in
ccish's own code (small build): `cccomp` 43,053 to 43,100 bytes,
`ccgen` 21,210 to 21,220, `ccrel` unchanged at 14,520. From before
`unsigned` (step 1's start) to now: `cccomp` 41,965 to 43,100, `ccgen`
19,972 to 21,220, `ccrel` 13,853 to 14,520.

**M7 design (decided in discussion, 2026-10-29).** `switch`, in one
step. ccish's own source has 26 switches with 218 case labels (the
largest has 28), so self-hosting needs it.
- **`SEL` tables from the start**, not a compare chain first (as the
  milestone list had it): a table entry is 4 bytes per case, a compare
  chain about 7 (`DUP CB v BREQ L`, plus keeping the value), so about
  650 bytes less in the self-hosted compiler, for about the same
  compiler code (decision 15).
- **Layout**, as PLASMA's compiler does it; the cases aren't known
  until the body is parsed, so the table comes after it:

  ```
  switch (c) {                    LLW c  SEL Ltab
  case 'a': x = 1; break;     La: CN 1 SLW x  BRNCH Lend
  case 'b': case 'c':         Lb: Lc:
      x = 2; break;               CN 2 SLW x  BRNCH Lend
  default: x = 3;             Ld: CN 3 SLW x  BRNCH Lend
  }                           Ltab: 3, 'a' La, 'b' Lb, 'c' Lc
                                  BRNCH Ld       (only with a default)
                              Lend:
  ```

  `SEL` consumes the value: no temporary, and a `return` in a case
  leaves nothing on the stack. With no `default`, a value with no case
  goes past the table straight to `Lend`.
- **Parser:** a case stack of (value, label) pairs, 64 entries (256
  bytes), shared by nested switches, each with its own base. At the
  `}` it sorts the switch's entries (the 6502 VM needs them in
  ascending signed order), which also makes the duplicate check a
  comparison of neighbors, and emits the table. Requiring cases in
  order instead would save only about 50 bytes of compiler, and would
  break real code, which groups cases by meaning (and, with `enum`,
  would depend on the enum's numeric order). `break` exits the switch;
  `continue` still goes to the enclosing loop.
- **IR:** `SEL` (the table's label), a table header (the count), and
  one entry per case (value and label).
- **Stage 3:** `SEL` is an unconditional jump, so code after it up to
  the first case label is dropped as unreachable (it can't run in C
  either). The table is never dropped as dead code.
- **Stage 4:** encodes the table; each entry's offset is resolved by
  the branches' fixup chains. The listing shows the table.
- **C semantics:** case labels can be anywhere in the body, in nested
  blocks and loops (Duff's device), since they only mark positions.
  Errors: a duplicate case value, a second `default`, `case` or
  `default` outside a switch, a case value that isn't an integer
  constant expression (a `const` variable isn't one), and a switch on
  a pointer. A switch with no cases gets a table with one dummy entry
  leading where no match goes.
- **Limits (errors, decision 16; rare in real code):** at most 255
  cases in one switch (the count is a byte), case values spanning at
  most 32767 (a 6502 VM rule), and 64 cases in all the switches open
  at once.
- Most of ccish's own case labels are `enum` constants, so `enum`
  (M11) is on the self-hosting path too. Until then, tests use numbers
  and character constants.

**`Sym` split design (decided in discussion, 2026-10-29; option B1 of
18.6).** Today every symbol is a 56-byte `Sym` (a 32-byte name and 12
`int`s) in three arrays of 512 entries in all: 28.7K, the largest
table. Functions and variables use mostly different fields. Split into
two tables of parallel arrays (decision 15):
- **Functions,** 10 bytes each: name (a name pool offset), return
  type, `ptypes` index, `nparams` (a `char`), a flags byte (declared,
  defined, called), and `code_off`.
- **Variables,** 9 bytes each, in one table used as a scope stack:
  globals at the bottom; a function's parameters and locals pushed
  above them and popped at its end (no locals exist while globals are
  declared). Name, type, offset, a byte holding the block depth plus
  bits for "in the data segment" (a global or static) and "has a known
  value", and the known value. **`has_val` stays** (`const` folding).
- **Names: the interned pool (A1), with local release.** Names first
  interned inside a function are released at its end, so the pool
  holds mainly file-scope names. A name that must outlive the function
  (such as a function first called there, an implicit declaration)
  must not be lost: the release has to keep it (to work out while
  building). Lookups compare pool offsets instead of `strcmp`.
- **Limits sized to slightly exceed ccish's own needs,** measured on
  its files (18.5), rather than round numbers.
- IR and machine ops carry a function index instead of a `Sym *`; the
  `.syms` format doesn't change.
- Built in three steps, each leaving every test's code unchanged: the
  functions table, the variables table, then names into the pool with
  local release.

**B3: the final pass as its own module (investigated and decided in
discussion, 2026-10-29).** Option B3 of 18.6: about 30K (the final
pass's 14.6K relocation table, 10.9K symbol table, 1.3K code buffer,
2.8K of code and 0.9K of strings) never shares memory with the
compiler. Start with **B3a** (two commands), built so that **B3b** (one
command, through a driver) is easy later.
- **How B3b works, with PLASMA as it is:** `cmdsys:modexec(name)` loads
  a module and runs its init; when init returns (below `modkeep`), CMD
  restores the heap and the global symbol table, freeing the module
  (`execmod` in `src/vmsrc/apple/cmd.pla`). It is what the shell runs
  for every `+MODULE` command, so it is well tested, on 128K machines
  too, unlike the overlay ideas of 18.4. A module reads its arguments
  from `cmdsys:cmdline`, a PLASMA string of the whole command line
  (81 characters at most), split by `args.pla`; the first word is the
  module's own name. If init returns a **negative** value, the loader
  sets `perr` from it and `modexec` returns it, so a driver can tell the
  compile pass failed.
- **The driver:** about 30 lines of PLASMA: read its arguments, write
  `CCCOMP FOO.C` into the command line buffer and `modexec` it, stop
  on failure, then write `CCREL FOO` and `modexec` that. The driver is
  named `CCISH`, so that the command is the same as on the host. It is PLASMA
  because ccish can't call through CMDSYS's table yet (function
  pointers, M11); it can become C later.
- **What B3a does now, so that B3b is easy:**
  1. Two complete entry points, each taking everything from its own
     command line and sharing nothing in memory, only the stream files:
     the compile pass and the final pass. The host `ccish` still runs
     both in sequence, for convenience and for the tests.
  2. In the split, the compile pass leaves the stream files and the
     final pass removes them after success; `-k` and `-l` are
     final-pass options, `-i` a compile-pass one. (Changed by issue
     11's step 3: `ccrel` keeps its inputs, as a linker keeps its
     objects, and `-k` is `ccish`'s alone.)
  3. Arguments short enough for the 81-character command line.
  4. Each pass closes all its files on every exit path (ProDOS needs
     it, and `modexec` doesn't do it; the shell does, after a command).
- **Later, also needed for self-hosting in general:** ccish's generated
  init drops `main`'s return value; it should return a negative value
  when `main` returns non-zero, so any C program reports failure to the
  shell (positive init values mean "keep the module" and must not be
  used). And `exit()` (M9 library) must unwind to the module's init with
  its status, since ccish's `error()` exits from deep in the parser.
- **`plvm` is a simulator, not the real system.** It interprets
  bytecode in C, over an emulated flat 64K memory, and implements
  CMDSYS and the loader natively in C (`src/vmsrc/cmdsys.c`); on an
  Apple II they are the 6502 VM and `cmd.pla`. So they can differ: we
  have already met differences in `NOT`, `SEL` and division by zero,
  and `plvm`'s `modexec` doesn't free the module when its init returns.
  That is fixed in a separate commit (38ef7b8f), written to be offered
  upstream, so that `plvm` imitates CMD's freeing. Also, `plvm` keeps
  module bytecode in host memory outside the emulated 64K (much as a
  128K Apple II keeps it in auxiliary memory), so code size doesn't
  count against its 64K at all. Still, `plvm` is right for
  testing that ccish's code is correct, but not for trusting memory
  numbers: the real free memory (E1), the loader's behavior and
  ProDOS's file buffers must be measured on the real 6502 VM and CMD,
  in an Apple II emulator or on a real machine. Since memory is now the
  main question, that setup (M13) may be worth bringing forward, at
  least for measuring.

**C2 design: messages in a file (decided in discussion, 2026-10-29).**
Option C2 of 18.6, in the user's form. About 7.9K of the compiler's
10.7K of string literals is message text, which is data in memory on
the Apple II, although at most one error is printed per run.
- **The message file is the single source of truth:** one text file,
  `ccish.msg`, each line `NUMBER TEXT`, sorted by number. It is the
  table of messages. No enum and no generated files: the program just
  records a number. To add a message, pick an unused number from the
  file.
- **Printing:** open the file and read lines until the matching number
  (stopping early, since it is sorted). The host programs and the Apple
  II both read the file; there is no in-memory copy. The number is
  always printed before the text (`file:2: error 123: text detail`),
  so if the file can't be opened, the number alone (`error 123:
  detail`) still identifies the message. Only on an error or warning,
  so speed doesn't matter (a few seconds from a floppy at worst).
- **Fixed text, variable parts last:** a message's text is fixed, and
  anything variable (a name, a token, a number) goes in the detail at
  the end. The few messages built with `sprintf` are reworded so (for
  example "no member named 'z' in 'struct pt'"). Fragments that were
  passed down to be part of a message (`need_lvalue()`'s "left operand
  of assignment", `check_assign()`'s "argument") become numbers too,
  printed after the message's text (`error 31 (212)` without the
  file). Where a helper reports only one message per caller, the caller
  passes a whole message number instead.
- **Readability:** each message number in the source carries its text
  in a comment, `error(123 /* "redeclaration of" */, quote(name));`,
  and `make test` checks that every such comment matches the file, that
  every number used is in the file, that every number in the file is
  used, and that the file's numbers are unique and ascending. These
  comments duplicate the file, so they will be removed later, after the
  other major changes, to shrink the source (18.6 F8).
- **Finding the file:** next to the program (on the host, the
  directory of `argv[0]`; on the Apple II, the prefix, then the system
  path), else the number alone. The reader accepts CR, LF or CRLF line
  ends (ProDOS text files end lines with CR).
- **Memory at the moment of an error:** `clean_up()` closes the other
  files first. On the host the reader uses stdio; on the Apple II, the
  M9 library's file reading must not need a big buffer of its own (or
  reuse one that is free by then), so that printing a message costs
  almost nothing.
- **Duplicates are free:** identical texts share one number (C2e).
- Saves about 7.9K of text, plus about 1K of fragments, for a small
  reader; and `error(123, ...)` is smaller code than `error("...",
  ...)` (a 2-byte constant instead of a 3-byte address). Messages could
  be translated by changing the file.

**C2 is done** (2026-10-29). Settled while building it:
- A one-time script turned every literal message (and `?:` choices of
  literals) into `NUMBER /* "text" */` and wrote `ccish.msg`, numbering
  in order of first appearance, with identical texts sharing a number:
  193 messages. The rest were converted by hand (messages 194-214):
  fragments became parts (`error_in()`, `warning_in()`;
  `need_lvalue()`, `check_assign()`, the `not_ice` details), helpers
  that report one message per caller take its number
  (`need_complete()`, `check_byval()`, `expect()`), and "'struct pt'
  has no member named 'z'" became "no member named 'z' in 'struct
  pt'".
- `error()` and `fatal()` now call `clean_up()` before printing, so the
  other files are closed when the message file is opened.
- The message file is found next to the program (`msg_init(argv[0])`);
  on the Apple II, the prefix-then-system-path rule waits for M9's
  library, as does the reader's buffering there.
- The error tests' expect lines were updated mechanically to include
  the numbers (`:2: error 41: ...`); only the reworded message needed a
  hand edit. `make test` runs `tests/check_msgs.sh` (shown to catch a
  changed comment, an unused message, numbers out of order, and a
  number with no comment) and checks the number-only output without
  the file.
- **Result, measured by self-compiling (18.5):** string literals went
  from about 10.7K to about 2.1K (in `parse.c`, 7.5K to 0.5K). Code
  went from about 32.9K to 33.5K, for the message reader in `diag.c`
  and the B3a programs, now measured as files of their own; the message
  calls themselves got smaller. What remains: the keyword table, the
  CMDSYS names, the IR names for `-i`, and the listing's opcode names.

**B3a is done** (2026-10-29). Settled while building it:
- Three programs from the same sources: `ccish` (both passes, as before),
  `cccomp` (the compile pass: `cccomp [-i] [-k] [-o output] input.c`)
  and `ccrel` (the final pass: `ccrel [-l] [-k] [-o module] output`).
  On the Apple II, the passes are the modules `CCCOMP` and `CCREL`.
  (The final pass was first named `cclink`; renamed `ccrel` on
  2026-10-29, decided in discussion, because it makes one REL module
  and links nothing across modules; PLASMA's loader does that. The
  name "link" stays free for a real linker, if one is ever needed.)
- `diag.c` holds what all three share: diagnostics, stream naming and
  removal, and the default output name. Each program defines
  `clean_up()`, which closes its open files and removes its partial
  output after an error (`compile_abort()`, `link_abort()`).
- The compile pass's body is `compile_file()` in `parse.c`.
- The `Bfile`s that were locals in `link.c` and `emit_finish()` are now
  static, so that an error can close them, and so that their 512-byte
  buffers take no frame space: two of the self-compile workarounds of
  18.5 are no longer needed.
- `make test` also checks that `cccomp` then `ccrel` write exactly
  `ccish`'s module for every run test, that `cccomp` leaves the stream
  files and `ccrel` removes them, and that a compile error leaves no
  stream files.

**B6a is done** (2026-10-29; 18.6 B6, decision 18). The compile pass
writes one stream file, `NAME.obj`, of tagged records, then the symbol
summary `NAME.syms`; object format version 3 (`OBJECT.md`). Every run
test's module and listing is byte for byte what the version 2 format
gave. Chosen while building it (open for review):
- **Names:** `.obj` for the stream, `.syms` kept. The tags are letters
  (`D` data, `R` data relocation, `S` string, `F` function), so that a
  dump of the file is easy to read.
- **`D` records hold up to 128 bytes** (`DATA_RUN`), with a count byte,
  from a 128-byte buffer in `emit.c`: 2 bytes of overhead per 128 (about
  1.6%; about 460 bytes on `link.c`'s 29K of zero-filled tables). A run
  is written when full, before an `R` record, and at the end; `F` and
  `S` records needn't end a run, since each step of the final pass reads
  only its own kinds. Alternatives: a count kept in the `Bfile`'s own
  block buffer and patched before the block is written (no extra buffer,
  but `emit.c` then depends on `bio.c`'s internals), or F6 (a run of
  zeros as a count), which would remove most of the data bytes too.
- **`R` records have no offset:** the word's place among the `D` and
  `R` records gives it, so an `R` record is 4 bytes (version 2's `.drel`
  records were 5), and the data's two bytes aren't written at all.
- **`S` records carry a length word and the characters;** the final pass
  adds the pool's length byte and NUL. One byte more per string than
  version 2's `.strs`.
- **One `Bfile` in `emit.c`,** for the stream and then for the summary
  (it opens after the stream closes): 4 fewer than before, plus the
  128-byte run buffer, so about 2K less static data in `cccomp`.
  `link.c` lost its second input `Bfile` (`rel`), about 0.5K.
- **Open files:** the compile pass has the source and `.obj` open (was
  5 files); the final pass has one input and the module (was 3). Both
  fit PLASMA's limit of 4, with room for the message file (C2) and,
  later, `#include` in the separate preprocessor (issue 2, "Revised").
- **The final pass reads `.obj` three times** (data, strings, code),
  skipping records with `next_record()`/`skip_record()`; slower from a
  floppy, as expected. Disk space is about the same: for the larger run
  tests, -19 to +11 bytes against the four version 2 files together,
  and three fewer files (their last blocks and index blocks).
- **Fixed on the way, an older bug:** a "cannot open" message could name
  the wrong file, since `error()` and `fatal()` remove the stream files
  (reusing `stream_name()`'s static buffer) before printing. `ccrel`
  missing `x.code` said "cannot open x.syms". They now copy the detail
  first (`stop()` in `diag.c`, a 200-byte local, which fits a PLASMA
  frame; only a host path over 199 characters is cut short).
- `bskip()` added to `bio.c`. New tests: `ccrel` names the missing
  `.obj` file, and rejects a stream cut short and one with an unknown
  tag (226 tests).

**Table sizes (2026-10-29; decided in discussion: "slightly exceed
ccish's need").** Every fixed table is sized just above the peak
measured on ccish's own files (with an instrumented build, and from the
self-compile streams of 18.5), about 10-20% more; the peaks are noted
next to each limit in `ccish.h`:

| Limit | Peak | Was | Now |
|---|---|---|---|
| `MAX_FUNC_CODE` (bytes in one function) | 1,053 (`peep.c`) | 4096 | 1280 |
| `MAX_FUNC_RELOCS` (in one function) | 112 | 256 | 128 |
| `MAX_RETURNS` (ENTER and LEAVEs in one function) | 23 | 128 | 32 |
| `MAX_RELOCS` (final pass, the module) | 2,263 (`parse.c`) | 2048 | 2432; gone with B4 |
| `MAX_LABELS` (in one function) | 90 (`lex.c`) | 256 | 104 |
| `MAX_TYPES` | 46 | 128 | 56 |
| `MAX_PARAM_TYPES` | 343 | 512 | 384 |
| `MAX_TAGS` / `MAX_MEMBERS` | 7 / 51 | 24 / 128 | 10 / 64 |
| `MAX_IDS` | 216 | 256 | 240 |
| `MAX_CASES` | 29 | 64 | 36 |
| `MAX_STR` (one string literal) | 85 | 512 | 256 |

- Saved at 16-bit sizes: about 6.0K in the compiler (2.8K of it the
  function code buffer) and 0.5K net in the final pass.
- `MAX_STR` stays at 256, not about 100, so that a full PLASMA string
  (255 characters, the `pstr()` limit) can be written.
- **`MAX_RELOCS` had to grow:** `parse.c` needs 2,263 relocations, more
  than the old 2,048, which the self-compile hadn't shown because it
  stops before the final pass. Holding them costs 6 bytes each (14.6K);
  streaming them instead (18.6 B4) would remove the table. **Done**
  (2026-10-30): B4 removed the table and the limit.
- `MAX_NEST` (32) and `MAX_PARAMS` (16) are unchanged: their arrays are
  small, and nesting wasn't measured.
- The test `local_init_frame.c` used a 300-character literal; it now
  uses 256.
- **Raised 2026-10-30** (decided in discussion), 10% above what
  `tools/measure.py` found ccish's own files need (18.5, "Programs
  linked and measured"), so that it compiles them with no workaround:
  `MAX_FUNCS` 272 to 302 (`parse.c` needs 274), `MAX_VARS` 72 to 82
  (`link.c`, 74), `MAX_PARAM_TYPES` 384 to 433 (`parse.c`, 393),
  `MAX_POOL` 5632 to 6212 (`parse.c`, 5,647). cccomp's data grows by
  about 1.1K. The peaks include the stand-in headers' declarations.

**The `Sym` split is done** (2026-10-29), in two steps (functions,
then variables with their names). Settled while building it:
- The limits come from measuring ccish's own files with an instrumented
  copy of the compiler: at most 245 functions, 59 variables visible at
  once (52 globals), and about 5,100 bytes of names, all in `parse.c`.
  So `MAX_FUNCS` is 272, `MAX_VARS` 72 and `MAX_POOL` 5632. At 16-bit
  sizes the symbol tables and pool go from 30.7K (28.7K of `Sym`s and a
  2K pool) to 8.9K (2.7K of functions, 0.6K of variables and the 5.5K
  pool). The pool is now most of it: every file pays about 2.5K for
  `ccish.h`'s names, which splitting the header by stage would cut.
- Releasing a function's names (`pool_release()`) moves the names of
  functions first seen inside it (implicit declarations, or `memset`
  and `memcpy` declared for initializers and struct copies) down to
  the mark, in order, and frees the rest. Its block-scope tags and enum
  constants are hidden by then; a hidden tag keeps its released name
  offset, which is harmless since nothing looks up a hidden tag. The
  test `names.c` checks it.
- A variable's depth and its two flags share a byte (`V_DEPTH`,
  `V_DATA`, `V_KNOWN`). An unnamed prototype parameter has name -1,
  which a lookup never matches, since `find_var()` returns early for a
  name not in the pool.
- `frame_too_big.c` reached the 255-byte frame with 128 `int`s, which
  `MAX_VARS` no longer allows; it now uses an array and three `int`s.
- `link.c`'s symbol table is sized by `MAX_FUNCS` too, and its own
  count of defined functions is `ndefs`.
- Every earlier test's code is unchanged, and the self-compiled code
  is about the same size (32.9K).

**`enum` and `typedef` design (pulled forward from M11; decided in
discussion, 2026-10-29).** Built right after M7, before M8, so that
ccish can compile some of its own files early and code sizes can be
measured instead of guessed (18.5). ccish's own source uses them
simply: anonymous `enum { A, B = 5, ... };` lists (211 distinct
constants, about 1.7K of names), and seven `typedef struct ... Name;`.
- **One compact table** for the ordinary identifiers that aren't
  variables or functions: enum constants and typedef names. Parallel
  arrays (decision 15): name (a name pool offset), value (a constant's
  value, or a typedef's type handle), and kind and block depth, about
  6 bytes per entry; 256 entries (`MAX_IDS`), about 1.5K.
- **Scope as in C:** a name declared deeper hides one declared less
  deep, whatever their kinds (`typedef int T; void f() { int T; }`
  works), found by comparing depths with the variables'. Names declared
  in a block are hidden at its end, like struct tags.
- **The name pool grows from 512 bytes to 2K:** the enum names alone
  need about 1.7K. Each `.c` file is compiled separately, and once
  `ccish.h` is split by stage (parse.c doesn't need the opcodes), each
  needs much less. The `Sym` split budgets the pool again.
- **`enum`:** `enum [tag] { A, B = 5, C, };` with integer constant
  expression values (which may use earlier constants) that fit in an
  `int`, and an optional trailing comma. A constant is an integer
  constant expression, so it works in case labels, array sizes and
  global initializers. An enum type is `int`, which C allows. **Known
  difference (decision 16):** `enum tag` isn't checked against a
  definition, so ccish accepts some wrong programs (a variable of an
  enum type never defined); it never miscompiles a right one. ccish's
  source has no enum tags.
- **`typedef`:** at file and block scope, with `*`, array sizes and
  qualifiers (`typedef struct node *NodeP;`, `typedef int Vec[3];`,
  `typedef const char *str;`). A typedef name starts a declaration, a
  cast or a sizeof type wherever a built-in type does, so `T * p;`
  declares `p`, as in C. Redefining one with the same type is allowed
  (C11). Not supported: typedefs of function types (no function
  pointers), or of arrays of unknown size.
- **Later, for self-hosting:** split `ccish.h` by stage so that each
  file reads fewer enum names (a change to ccish's source, not the
  language), before M12's measurements.

**`enum` and `typedef` are done** (2026-10-29). Settled while building
them:
- The table is in `name.c` with the name pool. Entries hidden at a
  block's end are freed when they are the last ones (no type refers to
  them, unlike struct tags), so block-scope names don't use it up.
- Lookup (`find_id()`) compares an entry's depth with the innermost
  variable of the same name; the deeper one wins. A redeclaration in
  the same scope is an error whatever the kinds (`int n; typedef int
  n;`), checked where variables, functions, constants and typedefs are
  added.
- `parse_base_type()` now applies qualifiers with `qualify()`, so
  `const Vec v` for `typedef int Vec[3]` makes the elements const, as
  in C.
- Every earlier test's code is unchanged.

**M7 is done** (2026-10-29). Settled while building it:
- Duplicate case values are checked as each case is parsed, against
  the switch's earlier cases, so the error points at the duplicate's
  line; the sort then only sorts.
- `MAX_CASES` (64) is below `SEL`'s 255-case limit, so that limit needs
  no check of its own. The span check is written so that it can't
  overflow with a 16-bit `int` (for self-hosting).
- The listing finds each table from its `SEL`'s target and lists it as
  `CASES`, `CASE` and `TO` lines, so a table isn't misread as code.
- Every earlier test's code is unchanged.
- Left for M10: a function that ends right after a switch keeps an
  unreachable `CN 0 LEAVE` (3 bytes), because the parser can't tell
  that nothing jumps to the switch's end label (the jump that would
  have, at the end of the last case, was dropped by stage 3). Stage 3
  could drop a label whose branches were all removed.

**M6 is done** (2026-10-29), after three steps; step 4 (`union`) was
dropped, below.

**M6 design (decided in discussion, 2026-10-29).** M6 is built in four
steps, each ending with all tests passing:
1. declarations and access: `struct tag { ... };` at file and block
   scope, anonymous structs, struct variables (global, local,
   `static`), arrays of structs, pointers to structs, `.`, `->`, `&`
   and `sizeof`, with errors for an unknown member, `.` on a
   non-struct, an incomplete struct used where its size is needed,
   and `==` on structs;
2. assignment and initializers: `a = b` copies with word moves up to
   4 bytes and CMDSYS `memcpy(dst, src, n)` beyond (its argument order
   is the order C reads `a = b`); flat brace initializers walk the
   scalar members in order, with C's brace elision
   (`struct pt a[2] = {1, 2, 3, 4};`), and nested braces stay in M11;
3. pass and return by value (issue 16): up to 4 scalar members return
   as a PLASMA multi-value return, larger structs through a hidden
   pointer. How to pass `char` members, since frame slots are words,
   is decided at the start of the step (decided then: see "M6 step 3
   design" below; by the words of the struct's bytes, up to 8 bytes,
   and no larger structs);
4. `union` (moved from M11): every member at offset 0, the size of
   the largest. **Dropped** (decided in discussion, 2026-10-29, before
   building it): ccish's own source has no unions (18.5), and even a
   cheap feature costs code and table space in the self-hosted
   compiler, which may not fit 64K (decision 17). `union` stays a "not
   supported yet" error, and is in the "later, if needed" list under
   Milestones. The design stands: the struct code with every member
   at offset 0, a union flag per tag (struct and union tags share one
   namespace in C, so the wrong kind is an error), the size of the
   largest member, and an initializer for the first member only, with
   the rest of the union zeroed.

Decided with it:
- **Layout:** members in order, with no padding (the 6502 has no
  alignment rules, and C allows any). This matches PLASMA's `struc`
  layouts byte for byte, so C can read PLASMA data structures.
- **Type table:** a struct is a new kind, `TK_STRUCT`, whose `tsub` is
  its tag number. Struct entries are never shared, since two structs
  with the same members are still different types. Qualifiers work
  unchanged (`const struct node *`).
- **New tables, as parallel arrays (decision 15):** about 1.5K in all
  (shrunk from about 4K after step 2, in discussion: the first sizes
  matched each other, not what programs need).
  - Tags (24): name, size (-1 while incomplete), and block depth (a
    `char`): 5 bytes each.
  - Members (128): owning tag (a `char`), name, type handle, offset: 7
    bytes each. (Changed while building step 1 from "first member and
    count" per tag: a struct defined inside another's member list adds
    its members in the middle of the outer one's, so a struct's
    members aren't always contiguous. Lookup scans all members for the
    tag, which is at most 128 compares.)
  - A name pool (512 bytes) holding each tag and member name once. The
    tables hold its 2-byte offset instead of a 32-byte array. ccish's
    own source, the largest program it must compile, has about 13
    structs and 60 distinct member names: about 400 bytes of names. A
    full pool is a clean error, and each size is one constant, retuned
    in M12 by measuring. The `Sym` split (18.4) can reuse the pool
    later, which is when its size gets budgeted for all names.
- **Self-reference:** a tag gets its entry when first mentioned and its
  size at its `}`, so `struct node *next;` works as an incomplete type.
- **Block-scope tags** record their depth and become invisible at
  their block's `}`, so an inner tag can shadow an outer one. They
  aren't reclaimed: reusing a slot could let a dead struct's type be
  mistaken for a live one (a static local's size is computed after its
  block ends), and the cost is a few bytes per tag, only in functions
  that declare types.
- **Member access** reuses the pending operands' constant
  displacement: `s.m` on a local is `LLW s+off` and on a global
  `LAW g+off`, `p->m` is `LLW p CB off ADD LW` (just `LLW p LW` at
  offset 0), and `a[i].m` adds `off` to the indexed element. `&s.m`,
  arrays in structs (`s.buf[i]`) and chains (`p->q->r`) follow.
- **`typedef` stays in M11.** Until then programs write `struct node`.

**M6 step 1 is done** (declarations and access). Settled while building
it:
- The name pool is its own file, `name.c` (`intern()`, `name_find()`,
  `name_of()`), so the `Sym` split can use it. Looking up a member
  uses `name_find()`, so a misspelled name doesn't grow the pool.
- Tags are hidden at the end of a block, after a prototype, and after a
  function body; file scope is depth 0. A tag first seen in a parameter
  list gets gcc's warning, since the same name defined later is a
  different struct (which then makes a prototype and a definition
  conflict).
- While a member list is parsed, its tag's size is -2, which catches
  `struct s { struct s { ... } x; }`; a member of the struct's own type
  is an incomplete type, as in C.
- Struct values aren't supported yet: assignment, initializers, struct
  parameters and struct returns each give "not supported yet", and any
  other use of a struct as a value (`a == b`, `if (s)`, `f(s)`) gives
  "a struct can't be used as a value here". `*p;` with a struct `*p`
  evaluates `p` and loads nothing.
- Through a constant pointer (`((struct regs *)0xC080)->status`), the
  member offset is added at compile time, so it is one `LAB` or `LAW`.
- Not folded yet: `(pts + 1)->y` is `LA pts CN 4 ADD CN 2 ADD LW`. The
  same happens for `p + 1` on its own; adding neighboring constants is
  a peephole pattern for M10.

**M6 step 2 is done** (assignment and initializers). Settled while
building it:
- The choice between word moves and `memcpy` is by estimated code size
  (`addr_cost()`, `access_cost()`), not a fixed 4 bytes: word moves
  only when neither side's address is on the stack already and they
  are no bigger than the call. So a 4-byte copy between locals is
  `LLW SLW LLW SLW` (8 bytes, the same as the call), but between
  globals it is `LA LA CN 4 CALL memcpy` (10 bytes, not 12). A 3-byte
  struct moves a word and a byte.
- `memcpy` takes the destination first, so when the source's address
  was emitted first (`c = *f()`), the destination's address is rotated
  ahead of it in stage 2's buffer (4(d2)), with a temporary only if
  the buffer overflowed. When the destination's address is on the
  stack already (`*f() = s`), it is kept with `DUP` for the result, and
  dropped if unused: 2 bytes that a later peephole pattern could
  remove.
- A struct copy calls the program's own `memcpy` if it declares one
  (as gcc does), which must have three parameters, C's order; a value
  it returns is dropped.
- The result of an assignment is the destination with `assigned` set,
  now for `K_VAR` too (only a struct's), so it is readable (`c = b =
  a`, `(b = a).y`) but isn't an lvalue and has no address.
- Initializers: `init_flat()` walks arrays and structs together over
  one flat list, and replaces the array-only walk. With no padding, the
  scalars are contiguous, so one "initialized so far" offset is enough
  to zero the rest and to size an array from its list. Every earlier
  test's listing is unchanged. A local may also be initialized from a
  struct expression (a copy); a global or static can't, since its
  initializer must be constant.

**M6 step 3 design (decided in discussion, 2026-10-29).**
- A struct of up to 8 bytes (`MAX_BYVAL`) is passed and returned as
  the words of its bytes: word 0 is bytes 0-1, and so on, first word
  deepest, with a last odd byte alone as a word. `ENTER` stores
  parameter words in consecutive frame words, so a struct parameter
  lands in its own layout, `char` members still packed, and is an
  ordinary local with no copy. For structs of `int`s and pointers this
  is issue 16's one value per member, so `struct qr { int q, r; }`
  still matches PLASMA's `divmod()#2`. The limit is 8 bytes, not "4
  scalar members": `{ char a, b, c, d, e; }` qualifies (3 words), and so
  do small arrays in structs.
- **A bigger struct can't be passed or returned by value:** that is an
  error, which says to use a pointer: a part of C left out on purpose
  (decision 16).
  ccish's own source never passes or returns a struct by value, and C
  code rarely does for anything bigger than a couple of words; on a
  6502 it would be slow and use frame space. Leaving it out saves a
  third of the step: caller temporaries and a hidden result pointer,
  callee prologue copies, and the copy at `return`. The design is known
  if a program ever needs it: the caller passes the address of a
  temporary as a hidden first argument for a result, and a callee
  copies a big parameter into its frame from a passed address.
- A small struct result on the stack (`K_VALUE` with a struct type) is
  stored straight into the variable or element it is assigned to, last
  word first; passed or returned on as it is (`return swap(p);` is just
  the call); dropped word by word if unused; and saved to frame
  temporaries (`materialize()`) for anything else, such as `f().x` or
  `*f() = g()`. A member of it is readable but isn't an lvalue.
- Passing a struct needs a prototype, since the caller must know the
  parameter is a struct.

**M6 step 3 is done.** Notes:
- `ENTER` takes the parameters' words, not the C parameter count; the
  `.syms` summary still records the C count.
- A function that falls off its end, or a bare `return;` (C89), pushes
  as many zero words as it returns, so the stack stays balanced.
- A struct passed from an address on the stack (`f(a[i + 1])`) is
  spilled first, unless it is a single word.
- A function returning an incomplete struct type is an error even in a
  prototype, which C allows; it is rare, and simpler to check once.

**M5 design (decided in discussion, 2026-10-28).** M5 is built in six
steps, each ending with all tests passing:
1. `sizeof`;
2. global initializers;
3. local array initializers;
4. rotation in place, replacing the spills that only fix order
   (added in discussion, 2026-10-29; see 4(d2));
5. `static`;
6. `const`, and warnings.

Step 4 (decided in discussion, 2026-10-29):
- Stage 2 gets the expression buffer, `ir_mark()` and `ir_rotate()`
  of 4(d2), with the two-temporary fallback when it overflows.
- A store to a computed address (`*d++ = *s++`, `a[i + 1] = x`,
  `*f() = x`) becomes `e addr SW`, with no temporary. When its value
  is used (`y = *f() = x`), the value is kept with `DUP` before the
  address (and masked to a byte for a `char`), instead of being read
  back through a saved address.
- `i + p` becomes `p i` scaled, then `ADD`, with no temporary.
- `*p op= e` evaluates `e` first when `e` has a call, which fixes the
  mismatch with C noted in the M4 step 3 notes. (Planned for
  commutative ops only; built for all of them.)
- Compound assignment and `++`/`--` on a computed address keep their
  spill: they need the address twice.
- Tests whose listings get better are updated, and new tests cover
  nested rotation and the overflow fallback.

Decided:
- **Global initializers are computed at compile time** (decision 10).
  An integer initializer goes through the normal constant folder, with
  code output switched off, and must fold to a constant. An *address
  constant* goes through a small separate parser, which accepts a
  string literal, `&name`, `&name[constant]`, an array name, and
  `name + constant` or `name - constant`, with casts. **Limitation
  (documented):** other forms that C allows, such as `2 + buf` or
  `&*p`, are errors ("initializer is not a constant ccish supports").
  (As built, `&m[1][2]` and `&a[2] + 1` are supported too: see the
  step 2 notes.) The alternative, a pending "address constant" operand in
  the expression parser, would accept any expression that folds to an
  address, but touches pointer arithmetic, comparisons and casts
  throughout. Revisit if real code needs it.
- **String literals move to their own pool** (a new stream file),
  which the final pass places after the globals. Otherwise, in
  `char *names[] = {"zero", "one"};` each literal would be written to
  the data stream in the middle of the array. Code references to
  strings get a new relocation kind, and pointers stored in the data
  segment get relocation records in another new stream. The object
  format becomes version 2. (The alternative, buffering an array's
  initializer in memory, would cap the size of tables, and
  self-hosting needs big ones.)
- **Brace lists are one level** (decision 10): `int t[] = {1, 2, 3};`,
  where an omitted size comes from the count, missing elements are
  zero, and a trailing comma is allowed. A flat list for a
  multi-dimensional array (`int m[2][3] = {1, 2, 3, 4, 5, 6};`) is
  valid C and allowed. Nested braces wait for M11. A scalar may have
  braces (`int x = {5};`). A `char` array may be initialized from a
  string literal (`char s[] = "hi";` has 3 elements, and
  `char s[2] = "hi";` has no NUL, as in C).
- **Local array initializers are element stores:** for example
  `CN 1 SLW t`, with pairs of `char`s stored as one word. Remaining
  elements are zeroed with `memset` when that is shorter than storing
  zeros. Elements may be non-constant (`{x, y}`), as C99 allows.
  Copying an image from the data segment with `memcpy` (9 bytes plus
  the data, so smaller from about 5 elements) is a possible M10
  optimization.
- **`sizeof expr` and `sizeof(type)`.** The expression is parsed but
  not run (code output is switched off, as for unreachable code). An
  array gives its full size, since arrays aren't converted to pointers
  until used; an array parameter gives 2. The type is `int`, since
  `unsigned` (C's `size_t`) waits for M11; sizes are at most 32767.
  A type name takes `*`s and array sizes, as declarations do.
- **`static`:**
  - on globals: accepted, and means what globals already mean
    (private). It will matter once globals can be exported.
  - on functions: accepted, and will keep them from being exported
    once exporting exists.
  - on locals: stored in the data segment like a global, with block
    scope (a `K_VAR` with the global flag). The initializer is
    constant and applied at load, once. It is also the C way to have a
    local array bigger than the frame (`static char line[256];`).
- **`const` is enforced**, as part of the type: `const char *` and
  `char *` are different types. Following gcc, assigning to or
  incrementing a `const` object is an error, and converting
  `const char *` to `char *` without a cast (which C requires a
  diagnostic for, and gcc warns about) is a **warning**. So ccish
  gets warnings: printed like errors, but compilation continues. String
  literals stay `char *`, as in C. A read of a `const` scalar with a
  constant initializer folds to the constant (`const int N = 10;`
  makes `x < N` use `CN 10`), but `&N` still works, and, as in C, `N`
  can't size an array, so ccish programs stay valid C.
  - **Pointers fold too** (decided in discussion, 2026-10-29). A
    pointer is a scalar, so a `const` pointer whose initializer is an
    integer constant cast to a pointer folds the same way:
    `char *const SPEAKER = (char *)0xC030;` makes `*SPEAKER` be
    `CW $C030 LB`, which the peephole stage turns into `LAB $C030`,
    the same code as `*(char *)0xC030`. Without the `const`, it is an
    ordinary global, and `*SPEAKER` costs two loads
    (`LAW SPEAKER LB`).
- **`volatile` is accepted and otherwise ignored** (decided in
  discussion, 2026-10-29; step 6). Real C needs it for memory-mapped
  I/O: an optimizing compiler such as gcc may delete
  `*SPEAKER;` unless `SPEAKER` points to `volatile char`. Accepting it
  lets ccish programs for the Apple II be correct C as well:
  ```
  volatile char *const SPEAKER = (volatile char *)0xC030;
  void click(void) { *SPEAKER; }
  ```
  - It is allowed wherever `const` is (declarations, casts, type names
    in `sizeof`), in either order with `const`.
  - It changes no code. ccish never drops or merges loads through
    pointers (M4 step 2 notes), so every access already happens as
    written.
  - **It is part of the type after all** (changed in discussion,
    2026-10-29, with the representation below): it costs nothing, so
    converting `volatile char *` to `char *` warns as gcc does, and the
    M10 optimizations of issue 17 that change loads (byte narrowing,
    duplicate loads) can see it. A `volatile` object is never folded,
    even if it is also `const` (`const volatile int x = 1;`).
- **How qualifiers are represented** (decided in discussion,
  2026-10-29): as bits in the type handle, not as type-table entries
  (the trick Clang's `QualType` uses). A type handle is an `int`: the
  low 8 bits are the index into the type table, and `0x100` is `const`,
  `0x200` is `volatile`. So `const char` is `TY_CHAR | 0x100`, and
  `char *const` is the `char *` index with `0x100`, with no table entry
  of their own. Only a pointer to a qualified type needs an entry
  (`const char *` is a pointer whose pointee handle has `0x100`), and
  C requires that one, since `const char *` and `char *` are different
  types. Removing qualifiers is a mask, and testing one is a bit test.
  The alternative, lcc's, makes every qualified type an entry of its
  own and adds a qualifier field to every entry.
  - Every read of the table goes through helpers in `type.c`
    (`type_kind()`, `type_sub()`, `type_count()`) that mask the bits,
    so no code indexes the table with a qualified handle.
  - An array is never qualified itself; its elements are
    (`const int t[3]` is an array of `const int`), as in C.
  - Operands carry unqualified types, with an `is_const` flag for a
    place, so that tests such as `type == TY_CHAR` still work.
- **The type table becomes parallel arrays** (decided in discussion,
  2026-10-29; see decision 15): `char type_kind[]`, `int type_sub[]`
  (a handle, with its qualifier bits) and `int type_count[]`, all
  `MAX_TYPES` long: 5 bytes per entry instead of 6.
  - When step 6 is done, `tests/run/speaker.c` changes to the form
    above, with `SPEAKER` and a `KEYBOARD` constant for `$C000`, and
    its listing checks stay the same (`LAB $C030`, no `CW $C0`).
- **Not in M5:** nested brace lists, exporting globals, and `extern`
  (wanted later). (`volatile` was here until 2026-10-29; it is now
  accepted in step 6, as above.)

**M5 step 6 (`const`, `volatile` and warnings) is done, so M5 is
done.** Chosen while building it (open for review):
- **Representation,** as decided above: qualifiers are bits in the type
  handle (`Q_CONST` `0x100`, `Q_VOLATILE` `0x200`), and the type table
  is three private parallel arrays in `type.c` (`char tkind[]`,
  `int tsub[]`, `int tcount[]`: 5 bytes per entry), read only through
  `type_kind()`, `type_sub()` and `type_count()`. Existing code
  generation is unchanged: every listing of the earlier tests is
  identical.
- **Operands carry unqualified types;** a place keeps its object's
  qualifiers in `Operand.quals` (from the variable, a pointer's target,
  or an array's element), so `&N` is a `const int *` and assignments
  can be refused. A parameter's or return type's own qualifiers are
  dropped, as C says (`f(const int n)` and `f(int n)` are the same
  function type), and so are a cast's (`(const int)x` is an `int`).
- **C's pointer rules:** a pointer may gain qualifiers on its target;
  losing one is a warning (C requires a diagnostic; gcc warns); deeper
  levels must match exactly (`char **` to `const char **` is an error,
  as in GCC 14). Comparisons and `p - q` ignore the targets'
  qualifiers, and `?:` gives a pointer to a type with both targets'
  qualifiers (`char *` and `const char *` give `const char *`).
- **Folding happens in `parse_primary()`,** when a variable with a
  known value is read, so the folder sees a constant (`N * 2` is 20).
  It is skipped when the variable is a place: followed by an
  assignment operator, `++` or `--`, or the operand of prefix `&`,
  `++` or `--` (`want_place`). A variable has a known value
  (`Sym.has_val`) when it is `const`, not `volatile`, a scalar, and
  initialized with an integer constant (a `char` keeps the low byte).
  `volatile char *const SPEAKER = (volatile char *)0xC030;` folds,
  since the pointer itself isn't volatile, so the speaker test now uses
  it and its code is unchanged (`LAB $C030`).
- **A folded value isn't a C constant expression** (`Operand.nonice`):
  it can't size an array or initialize a global or static ("a const
  variable's value isn't a constant expression in C"), and a
  `const int` holding 0 isn't a null pointer constant. The mark
  carries through folding, `&&`, `||` and `?:`. gcc accepts some of
  these as extensions; ccish keeps programs valid C.
- **Not folded:** a `const` pointer initialized with an address
  (`char *const msg = "hi";`, `= buf`), since a constant operand can't
  carry a relocation; it is loaded from memory. A `const` object still
  takes its space (a global's data bytes, a local's frame slot and
  store), since `&N` may be used later and a single pass can't know;
  recursion also needs a distinct local in each call.
- **CMDSYS signatures** now use `const` where the function only reads
  through a pointer: `puts(const char *)`, and the sources of `strcpy`,
  `strcat` and `memcpy` (letters `S` and `P` in `cmdsys_funcs[]`).
  So a program's own prototype must use `const` there too
  (`void puts(char *);` now gives "declaration doesn't match the
  CMDSYS function"); no test had one.
- **Warnings:** `warning()` in `main.c`, printed like errors
  (`file:line: warning: ...`), and compilation continues. The test
  runner gained `// stderr-has: TEXT` for run tests, and a run test
  without one must now compile with no messages at all, so a spurious
  warning anywhere fails a test.
- **Space added, to be recovered by the planned `Sym` split** (decision
  15): `Sym` gained two `int`s (`has_val`, `val`), and `Operand` two
  (`quals`, `nonice`), which could share one flags `char`. Each
  `Operand` lives on the parser's stack during recursion, so its size
  matters there too.
- **Not done:** a warning for a constant that doesn't fit in a `char`
  (`char c = 300;`, gcc's `-Woverflow`), possible now that warnings
  exist.
- **Tests:** `tests/run/const.c` (folding of global, local and static
  `const` values, `int const`, `&N`, `sizeof N`, a `const volatile`
  that is read each time, `const char *` parameters, comparisons and
  `?:`, and all four warnings with their positions), the updated
  `speaker.c`, and ten error tests (assignment and `++` to `const`
  variables, parameters, array elements, pointer targets and `const`
  pointers; a `const` variable as an array size, a global initializer
  and a null pointer; and `char **` to `const char **`).

**M5 step 5 (`static`) is done.** Chosen while building it (open for
review):
- **A static local is a global with block scope.** It goes in
  `locals[]`, so it leaves scope at the end of its block like any
  local, but its symbol is `S_GLOBAL`, at the next data offset. So
  everything that handles globals works on it unchanged: `LAW`/`SAW`,
  `LAB`, pending elements (`line[i]`), `&x`, `sizeof`, and use in
  another static's address constant (`static int *pa = &arr[1];`).
  It doesn't count toward `MAX_GLOBALS`, but does toward `MAX_LOCALS`
  while in scope.
- **Its bytes are written to the data stream where it is declared,**
  in the middle of the function, by the global-initializer code
  (`parse_global_init()`), so initializers take exactly a global's
  forms and must be constant. An automatic local's value or address
  gives "initializer is not a constant ccish supports", as C requires.
- **No frame space:** a function whose only locals are static has no
  frame (no `ENTER`), and `static char line[300];` works in a 4-byte
  frame. The "local array doesn't fit in the 255-byte frame" error now
  says "(make it static or global)"; its two tests changed to match.
- **Only a leading `static`** is accepted (`static int x;`). C also
  allows `int static x;`, which ccish rejects; it is rare. A type is
  required after it ("expected a type after 'static'"): C89's implicit
  `int` isn't supported anywhere.
- **`for (static int i ...)`** is an error, as C99 6.8.5p3 requires.
- **On globals and functions,** `static` is accepted and ignored, and
  the declaration needn't agree with others (`int f(void);` then
  `static int f(void) { ... }`). C11 6.2.2p7 makes that mix undefined,
  so no diagnostic is required; a check can come with exports and
  `extern`, when linkage starts to matter.
- **Tests:** `tests/run/static.c` (a counter that starts at 10, a
  300-byte static buffer, two blocks' statics with the same name,
  string, array and address initializers, and `static` on a global and
  a function), and error tests for a `for` declaration, an automatic
  value or address in an initializer, a missing type, and a
  redeclaration.

**M5 step 4 (rotation in place) is done.** Chosen while building it
(open for review):
- **The buffer** is in `ir_put()` (`lower.c`): 64 IR records (op,
  operand, symbol). It is lowered at the end of each full expression
  (`end_expr()` calls `ir_flush()`), at the end of each function, and
  when it is full. Statement-level ops (labels, branches) simply wait
  in it until the next flush.
- **Marks are plain IR positions** (`ir_here()`), counted from the
  last flush at the end of an expression, so the parser needs no
  open/close bookkeeping. A flush of a full buffer keeps counting, so
  `ir_holds(pos)` can tell that code before it is gone.
- **A store through a computed address waits** (a new operand kind,
  `K_STORE`) until the parser sees whether its value is used. Then
  `finish_store()` adds a `DUP` after the value if it is used, rotates,
  and emits the store. Doing the rotation at `=` would have meant always
  emitting the `DUP` and removing it with a peephole rule. That rule
  would fail on an address that contains a store (`*d++` is
  `LLW d DUP INCR SLW d`), or one longer than its window. The parser
  checks that nothing was emitted after the value in the meantime
  (an internal error otherwise).
- **Its value is the `DUP`'d value**, masked with `ANDI 255` when an
  `int` is stored into a `char`, not a read back through the address.
  That is C's value, and it reads no memory, which matters for I/O.
  (Pending elements, `*p = v` and `a[i] = v`, still read back.)
- **Held constants.** A constant held for folding (the `5` in
  `5 - (a[i + 1] = 2)`) is written by the first op of the lvalue's
  code, but belongs before it. `code_start()` skips them, using the
  count held when the operand's parsing began.
- **`i + p`** rotates when `i`'s code is still in the buffer:
  `LLW p LLW i IDXW`. `c + p`, with a constant `c` and a `p` that
  emits code (`2 + f()`), and `i[p]`, still use a temporary: both are
  rare, and the constant's position is harder to find.
- **Compound assignment to a non-indexed pending element** (`*p`,
  `p[3]`, `*gp`), whose address is computed twice, is now correct C
  for every operator (decided in discussion: correct C first, where
  that is reasonable). The element isn't loaded until the right side
  `e` is parsed (`compound_elem()`):
  - `e` emitted no code (a constant or variable): load first, as
    before, so that `CN 1 ADD` → `INCR` and the like still apply.
  - `e` has a call (counted by `parse_call()`): load after it, so both
    addresses follow the call. No rotation is needed, so this is right
    even if the buffer overflowed. The operator is applied reversed:
    commutative ops as they are (`bump() LAW gp LW ADD LAW gp SW`),
    `-=` as `SUB NEG` (1 more byte), and `/= %= <<= >>=` with `e`
    saved in a temporary first (4 more bytes, 2 frame bytes). A
    pointer's `e` is scaled before the load.
  - otherwise `e` can't change the pointer without undefined behavior,
    so the load is rotated ahead of `e`, keeping the usual order (`*gp
    += i * 2` is `LAW gp LW LLW i IDXW …`). On overflow, the reversed
    form is used; it is correct, just bigger.
  `tests/run/compound_call.c` covers every operator, a pointer and a
  `char`.
- **Why only calls matter** (C11): a function call is indeterminately
  sequenced with everything else in the calling expression (6.5.2.2p10),
  and a compound assignment evaluates its lvalue once and is a single
  evaluation with respect to such a call (6.5.16.2p3). So if `f()`
  changes `p`, `*p -= f()` must act on the old `*p` throughout or the
  new one throughout; it is unspecified which, but not undefined.
  Changing `p` directly in the right side (`*p += (p = q) != 0`) is
  unsequenced with the read of `p` on the left, so undefined (6.5p2,
  6.5.16p3), and needs no care. The other cases were already right:
  a variable (`x -= f()` loads `x` before the call, which C allows),
  and a spilled address (computed once, before the call).
- **Overflow fallbacks:** a store swaps with two temporaries; `i + p`
  uses its old temporary; compound assignment uses the reversed form.
  `tests/run/rotate_overflow.c` covers the first two.
- **`ccish -i`** now prints each IR op as it is lowered, so it shows
  the order after rotation.
- **Effect on the tests:** no listing checks needed changing. Frames
  shrank where temporaries went away: `fill()` in `pointers.c` from 8
  to 6 bytes, `sort()` in `arrays.c` from 12 to 10, and `copy()` in
  the new `rotate.c` (`while ((*d++ = *s++) != 0);`) has only its
  parameters. Its loop is `s++ LB DUP d++ SB` and a branch.
- **Still spilled, as planned:** compound assignment and `++`/`--` on a
  computed address, and compound assignment to an element with an
  index variable. Each needs the address twice (4(d2)).

**M5 step 3 (local array initializers) is done.** Chosen while
building it (open for review):
- **One parser for both.** Step 2's array-initializer code now serves
  locals too (`parse_array_init()`); only the three element actions
  differ: a scalar element, a byte, and a run of zeros, written to the
  data stream for a global, or stored into the frame for a local. A
  local's elements may be any expressions, as C99 allows.
- **Stores, as decided.** `int t[5] = {1, 2, 3};` is
  `CN 1 SLW t  CN 2 SLW t+2  CN 3 SLW t+4  CN 0 DLW t+6 SLW t+8`.
  - Two neighboring `char` constants (a string's characters, or
    `'a', 'b'` in a list) are one word store: `CW $6261 SLW s`, 5
    bytes, not 8.
  - The zeros after the last initializer (including a string's NUL
    just before them) are `CN 0` then a chain of `DLW`s ending in
    `SLW` for up to 3 words (1 + 2 bytes per word), and
    `LLA CN 0 CB n CALL memset` (7 or 8 bytes) for more.
  - CMDSYS `memset` fills with a 16-bit pattern (it writes `val`'s low
    and high bytes alternately), which is the same as C's only for 0.
    The compiler uses it only with 0. The README now warns programs
    that call it.
  - If the program defines or declares its own `memset` (one that
    doesn't match CMDSYS's), the compiler stores the zeros instead.
- **Sizes from the initializer** work for locals too. Such an array
  grows in the frame as its elements are parsed (`reserve()`), so that
  any temporaries an element needs come after it; the frame-size error
  still applies.
- **A scalar local may have braces** (`int y = {8};`), as globals may.
- **Cost of a long initializer:** each element is a store, about 3–5
  bytes per element. Copying an image from the data segment with
  `memcpy` (9 bytes plus the data) would win from about 5 constant
  elements; that stays a possible M10 optimization, as decided.

**M5 step 2 (global initializers) is done.** Chosen while building it
(open for review):
- **Object format version 2.** Two new streams: `.strs`, the string
  pool, and `.drel`, relocation records for words in the data (5 bytes
  each, like a function's records). Code references to strings use a
  new relocation kind, 3. The final pass places the pool right after
  the globals and applies `.drel` while copying `.data`. Its
  relocation list now holds load addresses, so that data and code
  words share it, and their RLD entries are written the same way.
  (OBJECT.md has the format.) The C VM loads modules at `$0300` and
  up, not at `$1000`, so the tests do exercise these relocations.
- **The address-constant parser** (`parse_addr()`) handles, besides
  the forms decided: any number of constant indexes after `&name`
  (`&m[1][2]`), and `+` or `-` after any address constant, including
  a parenthesized or cast one (`&a[2] + 1`, `(char *)buf + 1`). Both
  cost almost nothing. Integers go through the normal expression
  parser with output off; after a cast, only a unary expression, as in
  C. A string's address may go as far as one past its NUL, and a
  global's as far as the end of the data written so far (one past the
  end is valid C); further is an error ("initializer address is out of
  range").
- **Rows of a `char` array of arrays may be string literals** inside
  the one-level brace list (`char kw[][6] = {"int", "char"};`), since
  that is how keyword tables are written; it costs a few lines. Other
  nested braces wait for M11, as decided.
- **Sizes from the initializer:** `int t[] = {...}` and
  `char s[] = "..."`; for a flat list of a two-dimensional array, the
  count rounds up to whole rows (`int f[][2] = {1, 2, 3};` has 2). An
  empty list with no size is an error. A global array with no size and
  no initializer is still "array size missing".
- **Limits:** globals and the string pool are each limited to 32767
  bytes (the error names which). Previously strings counted against
  the data limit.
- **A `char` initialized with a too-big constant** keeps the low byte
  silently (`char wrap = 300;` is 44), as a cast would. gcc warns; a
  warning could come with step 6 (renumbered from 5 when rotation
  became step 4).
- **The speaker test** (`tests/run/speaker.c`) still uses a cast:
  `char *speaker = (char *)0xC030;` now works, but reads through it
  are `LAW speaker LB`, not `LAB $C030`, until `const` folding (step
  5).

**M5 step 1 (`sizeof`) is done.** Chosen while building it (open for
review):
- **`sizeof "hi"` is 3.** In C a string literal is a `char` array, but
  ccish gives it type `char *`, so that each literal doesn't add a type
  entry (an array of its length) to the 128-entry type table. Only
  inside a `sizeof` operand is a literal that array (a `K_DEREF` of
  array type, with no code). So `sizeof "hi"[0]` is 1, as in C.
- **`sizeof (a)[1]`** is the size of `(a)[1]`, as in C: after a
  parenthesized expression, postfix operators still apply (the postfix
  loop is now `parse_postfix()`). `sizeof(int) x` is a syntax error, as
  in C.
- **`sizeof` of `void`** (the type, or a `void` call) is an error. gcc
  gives 1 as an extension.
- **String literals in code that is never run no longer take data
  space** when the code is an expression parsed with output switched
  off (`0 && f("x")`, a `sizeof` operand). Literals in a statement that
  can never run (the body of `if (0)`) still do, because statements
  are compiled and then dropped by the peephole stage.
- **Added during step 1, at the user's request** (a speaker-click test,
  `tests/run/speaker.c`):
  - **Functions with no frame** (no parameters, locals or
    temporaries) have no `ENTER`, and `RET` in place of `LEAVE`, as in
    PLASMA's compiler (issue 17, P20, done as planned there). So
    `void click(void) { *(char *)0xC030; }` is `LAB $C030 DROP RET`.
    `main` is often frameless too.
  - **A load or store through a constant address is one op:**
    `CW $C030 LB` → `LAB $C030` (also `LW`, `SB`, `SW`; issue 17, P8),
    and after `CB` or `CFFB` too, which is the same size but one
    dispatch fewer. Not after `CN` or `MINUS1`, which would grow.
    `LA g LW` → `LAW g` (P7) comes with it for free.
  - The address is written as a cast for now. With M5's initializers
    and `const`, it can be named (`char *const speaker = (char *)
    0xC030;`), and const folding should give the same code; the test
    will be updated then.

**M4 design (decided in discussion, 2026-10-27).** M4 is built in four
steps, each ending with all tests passing:
1. a type representation, `char`, and casts;
2. pointers: `*`, `&`, pointer arithmetic and comparisons, stores
   through pointers, and `void *`;
3. arrays: local arrays, indexing, multi-dimensional arrays, and array
   parameters;
4. globals: scalars and arrays, zero at load.

Decided:
- **Types are entries in a table** (kind, element type, count), and a
  type is a small index into it. Equal types share one entry. It costs
  about 5 bytes per entry, and it handles multi-dimensional arrays and
  pointers to arrays now, and structs (M6) and function pointers (M11)
  later, without a rework. (The alternative was Small-C's base type
  plus pointer depth, with one-dimensional arrays only.)
- **Pointer types are checked strictly, as C requires.** Converting
  between an int and a pointer needs a cast, except for the constant
  0, and pointer types must match unless one side is `void *`. ccish
  has no warnings and stops at the first error, so these are errors
  (GCC 14 also makes them errors by default). So casts come in M4, and
  the CMDSYS table gets types (`heapalloc` returns `void *`, `puts`
  takes `char *`, ...).
- **Pointer `< > <= >=` are unsigned.** Addresses at or above $8000
  are normal (the heap reaches them on a 64K Apple II), and PLASMA's
  comparisons are signed. So each side gets `CW $8000 XOR` (4 bytes),
  then a signed comparison. No temporary is needed: the left side's
  type is known before the right side is parsed. M11's `unsigned`
  can reuse this. (Calling CMDSYS `isult` would be smaller but needs
  a call each time.)
- **Uninitialized global arrays are zero bytes in the data segment**
  (issue 11: REL has no BSS segment), so `char buf[4096]` adds 4K to
  the module file. **Possible, not committed (for M12):** the module's
  init allocates large arrays from the heap, and the global holds a
  pointer. Indexed access costs the same (`LAW` in place of `LA`), but
  constant-index access and `&buf` cost more, and it must work with
  how the heap is released when a program exits, and with resident
  libraries.

Defaults, also agreed:
- Plain `char` is unsigned (issue 7). `signed`, `unsigned`, `short`
  and `long` stay "not supported yet". A `char` local takes one frame
  byte. A `char` parameter is still passed as a word (`ENTER` pops
  words) and read with `LLB`.
- A store to a place known at compile time needs no temporary: locals,
  globals, array elements at constant indexes, and `*p` where `p` is a
  plain variable (`e LLW p SW`). This is issue 4(a), option A. Every
  other store (`a[i] = e`, `*f() = e`, `*p += e`) saves the address in
  a temporary frame slot first, as 4(e) says. Block exchange still
  waits for M10.
- **Pending array elements (decided in discussion, during step 2).**
  Saving an address in a temporary is a hand-made SWAP (4 bytes, 2
  ops and 2 frame bytes, against 1 byte for a real SWAP), and without
  this, `a[i] = x`, the most common store after plain variables, would
  need one every time. So in step 3, `a[i]` stays pending, like `*p`,
  when the array is a local or global array or a pointer variable,
  and the index is a variable or a constant. Nothing is emitted until
  the parser sees whether it is assigned to, and `a[i] = x` becomes
  `x LLA a LLW i IDXW SW` with no temporary (issue 4(a), option A).
  Only more complex lvalues (`a[i + 1] = x`, `a[f()] = x`,
  `*p++ = x`) still spill, until M10's block exchange or a VM change
  (issue 21).
- Pointer arithmetic scales by the element size (`IDXW` for 2-byte
  elements). `i + p`, with the pointer on the right, uses a temporary
  (4(b)). `p - q` divides by the element size.
- Globals are private to the module: not exported, and no `extern`
  yet (issue 11).
- No initializers on globals and no `sizeof` until M5. (Local
  initializers already work.)
- Local arrays must fit the 255-byte frame. A bigger one is an error
  that suggests making it global; open question 4 stays open.
- String literals and `pstr()` have type `char *`.

**M4 step 4 (globals) is done, so M4 is done.** Chosen while building
it (open for review):
- **A global is a variable like a local.** The parser's pending
  variable (`K_LOCAL`) became `K_VAR`, with a flag that says whether
  its offset is a frame offset or a data offset. A `K_ELEM`'s base
  (array or pointer) and its index variable can each be global too. So
  everything step 3 gives locals applies to globals, with no new cases:
  `g[i] = x` is `x LA g LLW i IDXW SW` with no temporary, `g[3]` is a
  variable at a fixed data offset, and an unused `g++` keeps no copy.
- **Globals have their own IR ops** (`IR_GLOBAL`, `IR_SETGLOBAL`,
  `IR_GLOBALB`, `IR_SETGLOBALB`), lowered to `LAW`, `DAW`, `LAB` and
  `DAB`. IR.md had planned `IR_DATAADDR` then `IR_LOAD`, fused by the
  peephole stage (`LA g LW` → `LAW g`, issue 17 P7). The parser knows
  a global's address at compile time, as it knows a local's frame
  offset, so mirroring the local ops needed no fusing patterns, and the
  local patterns carried over: `DAW n DROP` → `SAW n` and
  `SAW n LAW n` → `DAW n`. That last one also works across statements:
  `g++; a[i] = g;` is `LAW g INCR DAW g LA a LLW i IDXW SW`. (`DAB`
  keeps only the stored byte on both the C VM and the 6502 VM, as
  `DLB` does, so it is safe for `char`.)
- **A separate table of globals** (`MAX_GLOBALS`, 128), because
  `syms[]` holds the functions and is written to the symbol summary.
  A name is looked up as a local, then a global, then a function.
  **Memory note for self-hosting:** a `Sym` is about 50 bytes on a
  16-bit target, mostly fields that only functions use, so 128 globals
  take about 6.5K, and `locals[]` as much again. A smaller entry for
  variables would save most of that; worth doing in a later memory
  pass, not now.
- **Data layout:** each global is zero bytes of its size in the data
  stream, in declaration order, between any string literals. There is
  no alignment, since the 6502 needs none. More than 32767 bytes of
  globals and strings is an error at the declaration that crosses the
  limit.
- **Redeclaration is an error,** even where C allows it: C's tentative
  definitions (`int x; int x;`) are valid C, but rare outside headers,
  and `extern` isn't supported yet. A global can't share a name with a
  function, in either order. `int x, f(void);` (a function declared
  with variables) gives "declare functions separately from variables".
- **The frame-size error now suggests a global:** "local array doesn't
  fit in the 255-byte frame (make it global)". Open question 4 (large
  locals on a heap or C stack) stays open.
- **Not done:** initializers on globals (M5), `static` and `extern`,
  exporting globals to other modules (issue 11), and global names in
  the listing (it shows `; data+4`; the final pass doesn't know the
  names). `LA g LW` → `LAW g` is still unneeded, since only `*&g`
  produces it.

**M4 step 3 (arrays) is done.** Chosen while building it (open for
review):
- **One pending kind for elements.** Step 2's `K_DEREF_LOCAL` (the
  object at the address in a local word) became `K_ELEM`: the object at
  base + index × scale + disp, where the base is a pointer local or the
  address of a local array, and the index is a local variable (`int` or
  `char`) or absent. `*p` is `p[0]`, and a spilled address is a
  `K_ELEM` based on its temporary. Nothing is emitted until the parser
  sees whether the element is assigned to, as decided.
- **Which `a[i]` stay pending.** The base is a local array, a pointer
  local, or a row of a multi-dimensional array (`m[1]`, `m[i]`), and the
  index is a single identifier or number. The parser can't parse the
  index first and decide afterward, since for any other index the
  base's address must be emitted first, below the index. So it looks
  ahead: the token after the index's first token must be `]` (or
  `:>`) on the same line, with only spaces and tabs between
  (`next_is_rbracket()` in `lex.c`; skipping only blanks keeps the line
  number right for errors). So `a[i]`, `a[ 2 ]`, `p[n]` and `m[1][j]`
  are pending, but `a[-1]`, `a[N - 1]`, `a[(i)]` and an index split
  across lines are not. Those still work; a store into them just
  spills.
- **A `K_ELEM` holds one variable index.** `m[i][j] = x` computes the
  row's address (`LLA m LLW i CN 3 SHL ADD`), adds `j`, and spills.
  A constant index folds into the displacement, so `m[1][j]` and
  `m[i][2]` stay pending, and for a local array the displacement is
  part of the `LLA` offset (`m[i][2]` is `LLA m+4 LLW i ... ADD`).
- **A constant index on a local array is a plain local** (`a[2]` is
  `LLW a+4`), as decided. One whose offset falls outside the frame (an
  out-of-bounds index, such as `&a[300]`) is computed as an address
  instead. Out-of-bounds constant indexes are not errors, since
  `&a[n]` is valid C.
- **Compound assignment to an element with an index variable spills
  its address** (decided in discussion): `a[i] += x` is
  `LLA a LLW i IDXW DLW t LW x ADD LLW t SW`. Computing the address
  again (`… LW x ADD LLA a LLW i IDXW SW`) would be 1 byte larger and
  one op slower. It would also differ from C when the right side calls
  a function that changes `i` through a pointer: the load and the store
  would use different elements, but C evaluates the lvalue once. The
  spill costs 2 frame bytes, shared with the function's other
  temporaries (`tests/run/compound_elem.c`).
  - **Still computed twice:** `*p op= e`, `p[3] op= e` and an element
    with a constant index, where a spill would cost code. So they keep
    the issue if `e` changes `p` through a pointer, which needs `&p` to
    escape. (Fixed in M5 step 4: when `e` has a call, the load comes
    after it.) Step 2's `*p += f()` has always behaved this way. `++`/`--`
    also compute the address twice, which is always correct because
    there is no right side.
  - Plain `=` is fine, since C doesn't say whether the address or the
    value is computed first.
  - **A SWAP opcode** (issue 21) would fix every case with no temporary
    and no second copy: `addr DUP LW x ADD SWAP SW`.
- **Arrays turn into pointers lazily.** An operand of array type stays
  a place until its value is needed. `rvalue()` then pushes the address
  of the first element (`LLA a`, or nothing for a `K_DEREF`), and type
  checks use `value_type()`. So `&a` (a pointer to the array), `*a`,
  `a[i]` and `&(a)` see the array itself, and `sizeof a` will too
  (M5). `*a` and `**m` are the first element at a fixed offset, with no
  code.
- **Declarations.** Array sizes are constant expressions (parsed as a
  conditional expression), and must be positive. An array's total size
  must be at most 32767 bytes ("array too large"), so sizes fit in an
  `int`. A local array that doesn't fit in the 255-byte frame is an
  error ("local array doesn't fit in the 255-byte frame"). Since step
  4, it suggests making the array global. In a
  parameter, the first size may be left out or given, and is ignored:
  `int a[]` and `int a[10]` are `int *a`, and `int m[][3]` is a
  pointer to arrays of 3 ints. Later sizes are required. Array
  initializers wait for M5.
- **Declarators in parentheses** (`int (*p)[4]`) are not supported yet.
  Pointers to arrays still arise from `&a`, from a multi-dimensional
  array used as a value, and from parameters such as `int m[][4]`.
- **Peephole stage.** The window grew from 6 to 10 ops, and the rule
  that drops the copy for an unused `x++` now accepts any store whose
  address is computed only on the stack. A backward scan counts stack
  effects to find where the address starts (`expr_len()` in `peep.c`),
  so an unused `a[i]++` keeps no copy either. A new rule removes
  `CN 0 ADD` and `CN 0 SUB` (from `names[i][0]`, where the pointer is
  loaded).

**M4 step 2 (pointers) is done.** Chosen while building it (open for
review):
- **Stores through pointers.** `*e` is pending, like a variable, until
  the parser sees whether it is assigned to. If `e` is a plain local
  `p`, nothing has been emitted, and `*p = v` is `v LLW p SW`, with no
  temporary. Otherwise `e`'s value is on the stack when `=` is seen;
  it is saved in a temporary frame word (`SLW t`), and the store is
  `v LLW t SW`. Temporaries are freed at the end of each full
  expression (a statement's expression, a condition, an initializer,
  a `return` value, a `for` clause), so they cost frame space only
  while they're in use.
- **An assignment through a pointer doesn't keep its value.** PLASMA's
  `SW` doesn't, and keeping it would need a `DUP` before the address.
  Instead the value is read back from the object (`LLW p LW`) only if
  it's used, as in `x = *p = 0`. So `*p = v;` as a statement costs
  nothing extra. This is C's value (the object's value after the
  store), including the low byte for a `char`.
- **Loads through pointers are never dropped**, even when the value is
  unused (`*p;`, `(void)*p;`), since reading an Apple II soft switch
  such as `$C010` has an effect and ccish has no `volatile`.
- **Pointer arithmetic is done in the parser** with plain IR ops (no
  typed IR): `p + i` is `p i CONST 2 MUL ADD`, which the peephole stage
  turns into `p i IDXW` (`CN 2 MUL` → `DUP ADD`, then `DUP ADD ADD` →
  `IDXW`). `p - q` on 2-byte elements divides with `CN 1 SHR`.
  `c + p` with a constant `c` emits `p` first, with `c` scaled at
  compile time; `i + p` with a variable `i` saves `p` in a temporary
  (4(b)).
- **Comparisons:** `==` and `!=` accept the same pointer type, a
  `void *` on either side, or a null pointer constant. `< > <= >=`
  need the same pointer type on both sides. Each side's sign bit is
  flipped as decided (the right side's at compile time when it's a
  constant).
- **Multiplying by a constant** uses PLASMA's own optimizer's rules
  (`codeopt.pla`), discussed and added in this step: `* 2` → `DUP ADD`,
  `* 3` → `DUP IDXW`, `* 0` → `DROP CN 0`, `* 2^n` → `CN n SHL`, and
  also `* 1` → nothing. `x * 2` is `DUP ADD`, not `CN 1 SHL`: both are
  2 bytes, but on the 6502 VM `DUP ADD` takes about 52 cycles and
  `CN 1 SHL` about 59, since `SHL` is a general shifter that saves Y,
  checks for counts of 8 or more and loops (`plvm02.s`). `MUL` is a
  16-step loop, slowest of all. PLASMA's other rule, division by a
  power of two as `SHR`, is wrong for C's signed `/` (it rounds toward
  minus infinity, not zero), so it waits for `unsigned` (M11).
- **The peephole rule for an unused `x++`** now covers stores through
  a pointer (`LLW p SW`) and updates by a constant (`CN 2 ADD`, a
  pointer's `++`), not just `INCR`/`DECR` and local stores.
- **CMDSYS types**, from `cmdsys.plh` and what each routine does (a
  judgment call, since PLASMA is untyped): `putc(char)`, `puts(char *)`,
  `gets(char)` returning `char *` (the PLASMA string it read),
  `getc()` returning `int`, `heapalloc(int)` and `heapmark()` returning
  `void *`, `heaprelease(void *)`, `heapallocalign(int, int, void *)`,
  `memset(void *, int, int)`, `memcpy(void *, void *, int)`,
  `strcpy` and `strcat` taking and returning `char *` (PLASMA strings),
  `syscall(int, void *)`, and `int` for the rest.
- `*(char *)0xC000` works, but as `CW $C000 LB`; turning a constant
  address into `LAB`/`SAB` is 17, P8 (M10).
- Function pointers, and `void` parameters other than `(void)`, are
  still errors.

**M4 step 1 (types, `char` and casts) is done.** Chosen while building
it (open for review):
- The type table (`src/type.c`) has only the basic types so far:
  `void`, `char` and `int`, at fixed indexes. Pointers and arrays are
  added in steps 2 and 3.
- A `char` value on the stack is always 0–255: loads use `LLB`, and
  stores into a `char` (`DLB`/`SLB`) keep only the low byte. So a
  value is converted (`ANDI 255`) only for a cast or a `char`
  function's return value. A `char` argument is converted by the
  callee, which reads the parameter's word with `LLB`; the caller
  passes the value unchanged.
- `char` locals use separate IR ops (`IR_LOCALB`, `IR_SETLOCALB`), not
  a type on `IR_LOCAL`, since an IR op has one integer operand.
- A new peephole rule turns a constant 0–255 followed by `AND` into
  `ANDI`.
- Function parameter types are kept in one pool (`param_types[]`, 512
  entries in all), and a prototype and a definition must agree on
  them. Argument types aren't checked against them yet; with only
  `int` and `char` every argument converts. That comes with pointers
  in step 2.
- A cast is parsed with the primary expressions, since it starts with
  `(`; its operand is a unary expression, as in C. Its result is never
  an lvalue, and `(void)` discards the value.
- The type of `b ? x : y` is their common type, or `int` if one is
  `char` and the other `int`. Arithmetic and comparison results are
  `int`.

**M3 is done.** Step 3 (comparisons, `if`/`else`, `while`, `do`,
`for`, `break`, `continue`, `&& || ?:`, and fixup chains) was the
last. Chosen while building it (open for review):
- A comparison's value is `ISxx NEG`: PLASMA gives `$FFFF` for true on
  every VM, and `NEG` makes it C's 1 in one byte. When the result only
  feeds a branch, the peephole stage drops the `NEG`, fuses `ISEQ`/
  `ISNE` with the branch into `BRNE`/`BREQ`, and turns a comparison
  with 0 into `BRTRU`/`BRFLS`.
- `&&` and `||` use PLASMA's `BRAND`/`BROR`. An operand is made 1 or 0
  (with `CONST 0 NE`) only where C requires it, and not when it is
  already 1 or 0, such as a comparison. The parser tracks that.
- Code that a constant makes unreachable inside an expression (`0 &&
  x`, the unchosen side of `1 ? a : b`) is parsed with code output
  switched off. It is still checked, but a constant division by zero
  there is no longer an error, as step 1 anticipated for `?:`. In
  statements (`if (0)`, `while (0)`), the parser emits a jump, and the
  peephole stage drops the unreachable code after it. To make that
  work, the parser leaves out a forward label that nothing branches
  to.
- `for` uses jumps around the step clause for now (two extra branches
  per loop); the side buffer of issue 17 can remove them in M10.
- `for (int i = ...)` declarations are accepted (C99), scoped to the
  loop, since declarations are already allowed anywhere in a block.
- Each label is one table entry per function in stage 4 (256 labels,
  `MAX_LABELS`), holding the chain head until the label is placed and
  its offset after.
- The comma operator, `switch` and `goto` are still unsupported.

**M3 step 2 (streamed output and a final pass, 18.1) is done.** The
formats are in [OBJECT.md](OBJECT.md). Every module and listing the
tests produce is byte-for-byte the same as before. Chosen while
building it (open for review):
- Stream files are the output name plus `.data`, `.code` and `.syms`.
  There are exactly three per compilation unit (`.c` file), however
  many functions it has; "one function at a time" is only about
  memory. They are removed after success and after any error; `-k`
  keeps them. (On ProDOS the suffixes leave 10 characters for the base
  name.)
- **Possible, not committed:** when object files are kept for linking
  several `.c` files (issue 11), three files per unit would clutter
  the disk, and each ProDOS file costs a directory entry and at least
  one block. The final pass could then combine them into **one object
  file per compilation unit** (the summary, then data, then code), so
  the linker still reads each part in order without seeking.
- On the host, both passes run in one process (`main.c`). The final
  pass (`link.c`) uses none of the compiler's tables, only the
  streams, so it can be split into its own program or module later.
- The `.syms` summary also records each function's parameter count and
  whether it returns a value, for a future linker to check calls
  between files. It starts with a magic `CI` and a version byte.
- The function buffer holds 4096 bytes and 256 relocations
  (`MAX_FUNC_CODE`, `MAX_FUNC_RELOCS`). Beyond those, "function too
  large" or "too many calls and data references".
- The source file is read through the same 512-byte block buffers
  (`bio.c`), not just the streams.
- The final pass checks that the three files agree (sizes, offsets,
  indexes) and reports a damaged stream file if not.

**M3 step 1 (constant folding) is done.** Settled while building it:
- A constant right operand of zero for `/`, `%`, `/=` or `%=` is a
  compile error, and so is a constant shift count outside 0–15. Both
  are undefined in C, and the VMs disagree on them. (When `?:` arrives,
  this may need to become a warning for code in a branch that can't be
  taken, such as `0 ? 1/0 : 2`.)
- The left constant of a binary operator is held on a small stack
  until the right operand is parsed; any IR op written meanwhile
  writes it out first. For a commutative op with a plain variable on
  the right, `c op x` is emitted as `x c op`, so `1 + x` becomes
  `INCR`.

**M2 is done** (local variables, assignment, the integer operators,
`++`/`--`, dead-code removal and the first peephole patterns).
**M1 is done.** See `README.md`. Settled while building it:
- `pstr()` is the builtin used to pass literals to CMDSYS.
- `void` functions return nothing (issue 6).
- Called but undefined functions must be in a built-in table of CMDSYS
  functions (`cmdsys_funcs[]` in `src/parse.c`). The table supplies
  each function's parameter count and whether it returns a value.
  PLASMA functions can return 0, 1 or 2 values, so guessing wrongly
  would unbalance the stack. In M1, these names keep PLASMA's meanings
  (e.g. `puts` takes a PLASMA string). How C's own `puts` and friends
  will coexist with them is part of open question 7.
- The program is named `ccish` for now (open question 9).

Each milestone ends with its tests passing under `plvm`. A test
compiles a C file, runs the module, and compares its output with the
expected output. Earlier tests must keep passing. The order after M1 is
a proposal.

**M1: end-to-end skeleton.**
- Language:
  - function definitions with `int` parameters and a `return`
    statement;
  - expressions: `int` constants, `+`, calls, and string literals as
    call arguments.
- Every pipeline stage exists, but in its simplest form:
  - the lexer and a precedence-climbing parser (with only `+` for
    now);
  - an IR with a handful of ops;
  - a 1:1 lowering stage and a pass-through peephole stage;
  - an emitter that writes a REL module, including fixups for data
    and calls and the RLD/ESD tables.
- Calls to functions not defined in the file become imports from
  CMDSYS (uppercased), so the test can use `puts` and `puti`.
- The module's init function calls `main` and **drops** its result,
  then returns 0. (A positive init return value would be read as
  "keep module" flags.)
- Example test:

  ```c
  int add(int a, int b) { return a + b; }
  int main() { puts(pstr("Hello")); puti(add(2, 3)); return 0; }
  ```

  The expected output is `Hello5`.
- Open choice: how M1 passes a string to CMDSYS `puts`, which expects
  a PLASMA string. The example assumes the `pstr()` builtin (issue 8,
  option A), which takes little code. See open question 4.

**Later milestones** (proposed order):
1. **M2:** local variables, assignment, and the remaining integer
   operators: `- * / %`, the bitwise operators, shifts, and the unary
   operators.
2. **M3:** in this order:
   1. **constant folding in the parser**;
   2. **restructuring the emitter into streamed output plus a
      separate final pass** (issue 18.1), before labels exist;
   3. comparisons, `if`/`else`, `while`, `do`, `for`, `break`,
      `continue`, and `&& || ?:`, with labels resolved by **fixup
      chains** (issue 18.2).

   Constant folding (decided in discussion) uses *pending constants*,
   which extend M2's pending variables:
   - A constant isn't emitted when parsed; the parser keeps its value.
   - Unary `- ~ !` on a pending constant, or a binary operator with
     two pending constants, computes the result at compile time.
   - `IR_CONST` is emitted only when the value is needed on the stack.

   This needs no lookahead and no tree, and it handles `-5`, `2*3`,
   `-(1<<4)`, `'a'+1` and so on. (The lexer can't do it: in C, `-5` is
   unary minus applied to 5, and only the parser can tell that from
   the binary minus in `a -5`.)

   Folding must follow C's rules for a 16-bit `int`: wraparound, `/`
   truncating toward zero, `%` taking the sign of the dividend, and
   comparisons giving 1 or 0 (not PLASMA's `$FFFF`). Division by a
   constant zero is a compile error. It also provides the constant
   expressions that M4 (array sizes), M5 (global initializers) and M7
   (`case` labels) need. The peephole rule that folds a constant
   followed by `NEG`/`COMP` stays, as a backstop for constants that
   only meet after lowering.
3. **M4:** `char`, pointers, arrays, `&` and `*`, and globals.
   Stores through pointers, using spills (4(e)).
4. **M5:** simple initializers, `sizeof`, `static` and `const`, and
   block exchange by rotation in place (4(d2)).
5. **M6:** `struct` (done).
6. **M7:** `switch`, with `SEL` tables (see "M7 design").
7. **M8:** the minimal preprocessor (3B), as a separate program
   writing a compact `.i` file (issue 2, "Revised").
8. **M9:** a runtime library in C-ish: file I/O over FILEIO, memory
   and strings.
9. **M10:** optimization: the `for` side buffer,
   fused FOR opcodes, and peephole patterns.
10. **M11:** `unsigned`, varargs (`printf`), function pointers and
    nested initializers. (`enum` and `typedef` moved up, to right
    after M7; see "`enum` and `typedef` design".)

    **Later, if needed** (decision 17): features ccish's own source
    doesn't use, added after self-hosting fits, if programs need them
    and there is room: `union` (designed in "M6 design", step 4).

    Notes for `unsigned` (from discussion, 2026-10-29), to design then:
    - Support may be limited, since unsigned is slower in PLASMA: `/`
      and `%` only by powers of two (shifts and masks), and an error
      otherwise. `>>` needs a mask after `SHR`, which is arithmetic.
      `< > <= >=` use the pointer comparisons' sign flip, folded for a
      constant side. Everything else is the same bits as `int`.
    - C converts `int` to `unsigned` silently when they meet in a binary
      operator (the usual arithmetic conversions), so `-1 < 1u` is
      false. A warning when that makes a comparison or division
      unsigned (like gcc's `-Wsign-compare`) would show the cost.
      `char` promotes to `int`, so it never makes an expression
      unsigned.
    - With a 16-bit `int`, an unsuffixed hex constant that doesn't fit
      in `int` is `unsigned` (`0x8000`), and a decimal one is `long`
      (`40000`, and so `-32768`). C has no suffix for a signed `int`:
      write `~0x7FFF` or `-32767 - 1`. ccish treats `0x8000` as the
      `int` -32768 until `unsigned` exists.
11. **M12:** self-hosting under `plvm` (the bootstrap test in issue
    15), then fitting it into the 64K budget. Since plvm's memory is
    64K too, the fitting can't wait until after self-hosting: see the
    assessment in 18.5 (the tables alone are about 66K today).
12. **M13:** a 64K Apple II in an emulator, then real machines.

The compiler is written in standard C and built with gcc throughout.
For self-hosting to be reachable without a rewrite, its source should
stay within the features planned for M11. That means no floating
point, `long`, bitfields or tricky macros. This is a guideline from
the start, not a gate on early milestones.

The draft code in `src/` is a throwaway prototype (see issue 15).
Pieces may be reused, such as the lexer and the REL module writer, but
M1 is built fresh around the pipeline.

## Open questions for the next round

1. **Memory budget.** Agree to measure free heap on 64K first, and
   make it a tracked budget?
2. **Features to drop or postpone for 64K.** Candidates:
   - function-like macros;
   - bitfields;
   - old-style (K&R) function definitions;
   - struct passing and returning by value (pointers only);
   - complex initializers (nested aggregates);
   - `goto`;
   - floating point and `long`.

   Which are acceptable to drop or postpone?
3. **Milestone order.** Is the M2–M13 order right? In particular,
   should the preprocessor (M8) or the runtime library (M9) come
   earlier, so that test programs can be more realistic sooner?
4. **Large locals (issue 6):** heap/C-stack allocation (6A), or static
   (6B, breaks recursion)? Until this is decided, they can be a compile
   error. (Since M5 step 5, a program can write `static` itself, as C
   allows, and the error suggests it. The open part is whether an
   automatic local bigger than the frame should work too.)

   **Estimate (discussed 2026-10-29; deferred to M12).** PLASMA's
   frame can't grow: `ENTER` takes a one-byte size, and `LLW`/`LLA`
   one-byte offsets. So large automatic locals need a software "C
   stack":
   - **Runtime:** a stack pointer `csp` and a region for it, set up by
     the module's init code (say `heapalloc` of a size given by a
     compiler option); one per module until M9's runtime library.
   - **Entry,** only in functions that need it: `LAW csp DLW save CW N
     ADD SAW csp` (about 11 bytes; `N` patched at the function's end,
     as the `ENTER` size is), plus an overflow check (about 10 bytes
     and an error routine), since a deep recursion would otherwise
     overwrite the heap silently.
   - **Each return:** `LLW save SAW csp` (4 bytes) before `LEAVE`.
   - **Access:** a large array is a pending element based on the
     pointer in `save`, which the operand model already handles (like
     `p[i]`). `a[i]` costs the same as in the frame; `a[2]` is
     `LLW save CB 4 ADD LW` (5 bytes) instead of `LLW a+4` (2).
   - **Total:** about 150 lines of compiler, and about 25 bytes and 2
     frame bytes per function that uses it.

   Against it:
   - **Memory is limited in the first place.** On a 64K machine, a
     region has to be reserved up front and sized by guesswork, since
     recursion depth isn't known, and it is lost to everything else
     even when unused. Programs that fit in 64K rarely want large
     buffers in recursive functions; they make them global or static.
   - Every exit path must restore `csp`: a future `longjmp`, or a
     PLASMA `throw` that unwinds through C code, would leak the space.
     Multitasking (issue 19) would need a C stack per task.

   Rejected: `heapalloc`/`heaprelease` at entry and exit, since
   `heaprelease` also frees anything allocated during the call (a
   long-lived buffer, or a loaded module), which breaks correct
   programs; and a VM change, which costs far more.

   So for now, large locals are an error that suggests `static` or a
   global. Revisit at M12, if compiling ccish's own source shows a
   need.
5. **Newlines (issue 9):** `'\n'` = 10 with stdio translation (9B)?
6. **Calling PLASMA routines (issue 8):** the `pstr()` builtin, used
   from M1? The alternative for M1 is to support only `puti` and
   `putc`, and leave strings until a runtime `puts` exists.
7. **Linking (issue 11):** a single translation unit first? How should
   names that collide on export (case, 16 characters) be handled?

   Answer (2026-10-30): separate compilation, statically linked by
   `ccrel` into one module (decision 20). Nothing is exported to
   PLASMA, so the collisions don't arise.
8. **Hints (issue 10):** defer until after M10?
9. **Name:** the doc says "C-ish". Should the program be called
   `ccish`, and should the predefined macro be `__CCISH__`?
10. **Unix-like command set (issue 19, possible, not committed):**
    should M9's library follow POSIX names and behavior, and should a
    limited `long` (or `int32` helpers) come before M11? Is multitasking
    option A (switching when a process waits) the right first step?
11. **New VM opcodes (issue 21, possible, not committed):** add
    reversed stores (`SWR`, `SBR`), unsigned comparisons, and perhaps
    `SWAP`? If so, when, behind which ccish option, and can they go
    upstream?
