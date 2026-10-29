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

Answer: B.

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
| `extern`, and linking 9 `.c` files | 11 | issue 11; no milestone yet |
| `long` | 1 (`lex.c`, number overflow) | none: rewrite it |

Nothing else: no function pointers, `goto`, `union` or comma operator.
So, apart from `long` and multi-file linking, the source keeps to the
features planned up to M11, as intended (issue 15). **Open:** link
separately compiled modules (`extern` and exports, issue 11), or build
the compiler from one file.

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
- **B5. Smaller or no code buffers:** two 4K function buffers to 2K (a
  function-size limit; ccish's largest so far is about 1K), or write
  code straight to disk and patch it by seeking (slower).
- **B6. Fewer open files:** ProDOS needs a 1K buffer per open file, and
  today a compilation has the source and 4-5 stream files open (5-6K).
  One combined stream file makes it 2K, but changes OBJECT.md.

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
- **C2. Smaller messages:** shorter text (11K to perhaps 5K), or error
  numbers with the text in a disk file read only on an error (to about
  1K), or numbers only, explained in the documentation.
- **C3. Drop C semantics that cost compiler code**, with "not
  supported" (or a simpler accepted form) instead. Candidates:
  block-scope tags, typedefs and enum constants (file scope only);
  implicit function declarations (require prototypes); the exact
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
  optimizer is more code.
- **C5. Restructure ccish's own source for size:** smaller functions,
  table-driven code instead of `switch` chains, `ccish.h` split by
  stage. No language change; gains hard to predict.

**D. Program structure.**
- **D1. One program** (the preference in issue 1): simple and fast, but
  needs aggressive versions of A-C, and may not be reachable.
- **D2. Compiler plus final pass as two modules** (B3): little work,
  already planned.
- **D3. More passes:** preprocessor to a temporary file, parser to an IR
  file, then a backend. Each fits easily, as in many small-machine C
  compilers, but floppy I/O makes it much slower.
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
  compiler, 3K for the final pass) and 10.7K of strings.

View when listed (not a decision): E1 and E2 first, since they set the
target; then the biggest levers look like B1 (`Sym` split), B3 (final
pass as its own module), C2 (message text out of memory), and C1 (a
lean self-hosted build). A2/A3 are attractive but risky without
collision detection; A4 is the C89-sanctioned form.

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
   (issues 1, 2).
2. **Preprocessor:** none at first; then the minimal one (3B), built
   in. A separate preprocessor program only if large programs need it
   (issues 2, 3).
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


## Milestones

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
7. **M8:** the built-in minimal preprocessor (3B).
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
