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
  existing PLASMA VMs.

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
Measure before considering SWAP again.

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
  the file unless the init function allocates them.

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
  use a hidden pointer.
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
  keeps every later offset the same.

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
   (4(e)).
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
    - `struct`, `switch` and simple initializers. Simple initializers
      are needed for real-world use. "Simple" means:
      - scalars and pointers (`int n = 5;`, `char *s = "hi";`,
        `int *p = &x;`);
      - `char` arrays from string literals;
      - one-level brace lists (`int t[] = {1, 2, 3};`).
    - Later: `unsigned`, `union`, varargs, `typedef`, `enum`, function
      pointers and nested initializers.

    Implementation notes on initializers:
    - Global initializers are folded at compile time.
    - Address constants in the data segment need relocation (RLD)
      entries.
    - Local initializers compile as ordinary assignments.
11. **Multiple returns:** a returned struct of up to 4 scalar members is
    a PLASMA multi-value return (issue 16). Larger structs use a
    hidden pointer. This lands with `struct` (M6).
12. **Streamed output, a separate final pass, and fixup chains**
    (issue 18.1 and 18.2):
    - The compiler writes a data stream, a code stream (each function's
      code followed by its relocation records), and a symbol summary.
    - A separate final pass, which doubles as a linker, writes the REL
      module.
    - Forward branches use fixup chains within a function buffer.
    - All file I/O uses 512-byte block buffers.


## Milestones

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
4. **M5:** simple initializers, `sizeof`, `static` and `const`.
5. **M6:** `struct`.
6. **M7:** `switch` (as a compare chain first).
7. **M8:** the built-in minimal preprocessor (3B).
8. **M9:** a runtime library in C-ish: file I/O over FILEIO, memory
   and strings.
9. **M10:** optimization: block exchange, the `for` side buffer and
   fused FOR opcodes, peephole patterns, and `SEL` tables.
10. **M11:** `unsigned`, `union`, varargs (`printf`), `typedef`,
    `enum`, function pointers and nested initializers.
11. **M12:** self-hosting under `plvm` (the bootstrap test in issue
    15), then fitting it into the 64K budget.
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
   error.
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
