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
- **C. Phases as PLASMA modules loaded and unloaded in turn.** PLASMA
  can do this. It is a middle ground between A and B, but only helps if
  the phases don't need each other's data.

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
4. **Assembler.** Turns labels into relative offsets and fixups into
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
- Avoid `NOT`, `DIVMOD` and `NATV` because of the differences between
  the C and 6502 VMs (see `../doc/Bytecodes.md`).

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

## Milestones

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
  - an assembler that writes a REL module, including fixups for data
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
2. **M3:** comparisons, `if`/`else`, `while`, `do`, `for`, `break`,
   `continue`, and `&& || ! ?:`.
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
