# Options for making cccomp smaller

An analysis, after the enum-grouping work (commits through 3ac38d25),
of where cccomp's size goes and what could still be done, in its own
source and in the code ccgen generates for it (and, through the same
peephole, for user programs). The numbers are from
`python3 tools/measure.py` on this tree, `tools/fsize.py` on its
listings, and a one-off opcode profile of the tests (section 4).

**Cost policy (settled while working section 5):** an optimization may
freely grow ccgen -- ccgen getting bigger to make the generated code
better is ideal -- but not cccomp: a few bytes in cccomp is fine,
a kilobyte is not, for now. An optimization that needs a significant
addition to cccomp is recorded here for later, not built.

## 1. Where cccomp's size goes

The loader reads a whole module file into memory before running a
program, so the binding limit is the module file, under about 40K of
free memory (AGENTS.md, "The goal"). The small build, after the
grouping work:

| Program | Code | Data | Strings | Module file |
|---|---|---|---|---|
| ccpp   | 13,780 | 3,364 | 1,070 | 26,012 |
| cccomp | 26,015 | 3,172 |   554 | 42,613 (still over) |
| ccgen  | 15,590 | 2,753 |   782 | 26,244 |
| ccrel  |  7,713 | 1,989 |   282 | 14,200 |

cccomp's module file is code plus data plus strings (29,741), plus
the RLD and ESD the linker appends (12,398, of which most is RLD:
four-byte relocation entries, one per reference to data or a function
in the code, and one per function), plus the header and symbol
summary. Two consequences:

- The RLD is about 12% of the module and tracks the code: cccomp
  has roughly one relocation per nine bytes of code (its code is full
  of `LA` of message strings and of its tables). Every byte of code
  saved takes about a tenth of a byte of RLD with it, so a code
  saving shows up in the module file about 1.12 times bigger.
- The RLD and ESD are freed after the module is linked into memory
  (measure.py's "freed after"), so the memory a running compiler
  needs is the 29,741, not the 42,613; it is the *load* that needs
  the whole file.

The code (26,015) is the biggest piece, and most of it is one file:
parse.c compiles to about 20.8K of the small build's code, the rest
of cccomp is tokin (846), nam (541), name (224), type (1,049), irout
(948) and the cccomp driver (187). parse.c is 153,389 bytes of
source. The top functions, from the listing:

| Function | Bytes | What it is |
|---|---|---|
| parse_expr      | 906 | the expression loop (precedence climbing) |
| parse_toplevel  | 725 | declarations at file scope |
| parse_primary   | 682 | identifiers, literals, ( expr ) |
| fold            | 508 | constant folding of `expr op const` |
| binary          | 506 | type checks for `l op r` |
| parse_unary     | 472 | `& * - ! ~` and casts |
| parse_declaration | 460 | specifiers, declarator, init |
| copy_struct     | 442 | struct copy for = and by-value |
| parse_index     | 439 | `a[i]` and `p[i]` |
| next            | 431 | fetch and check one token |
| parse_statement | 409 | the statement switch |
| rvalue          | 354 | load an operand's value |

The data (3,172) is mostly the function tables, sized by MAX_FUNCS
(280: 254 user functions plus the helpers' slots): fn_name 560,
fn_ptypes 560, fn_nparams 280, fn_flags 280, fn_type 280 -- 1,960 of
the 3,172 -- the rest being the input and output buffers (in, out:
522 each), the id and type tables (id_nm, id_v: 406 each,
param_types 401) and the token buffer (tok_text 256). The strings
(554) are cccomp's share of the message pool (303 messages, about
9K of text, in ccish.msg).

## 2. Making cccomp's own code smaller

In rough order of expected saving per unit of effort. None of these
changes what the compiler emits for a user program, so each is
verified the same way the grouping work was: tests, the small build's
byte-identical objects, and `tools/opcount.py` against a worktree
expecting zero changes.

**A1. The expression parser, and the code around it.** parse_expr,
parse_unary, parse_primary, parse_index, rvalue, binary and fold
together are about 4.1K, a sixth of the program. Ideas, in order of
how much they seem worth:

- `fold()` (508) computes one result per IR op by long `if` chains
  over the op. A table of 64 entries (one per op tag) of "how to fold
  this op, or not" would be about the same size as the code, so the
  win is in the *shape*: the current chains test the same ops in
  several functions (fold, unsigned_op, check_right_const; the IR_IS_*
  macros of the grouping work already share the *sets*, not the
  code). Whether a second, fold-specific table pays is not clear
  until measured; treat as an experiment.
- `parse_primary` (682) and `parse_unary` (472) each re-derive "an
  operand that is already a value on the stack" (rvalue()'s cases).
  Factoring the operand->value path into one function they both call
  would remove the duplication; the cost is one more call at the hot
  spots, which on this VM is a few bytes each.
- `next()` (431) checks every token against the parser's limits and
  message table. Its message calls are also a good share of the RLD
  (every `error(n, ...)` is an LA of the message). Fewer, broader
  messages (section A3) shrink both.
- The small build already keeps size first: CCISH_FAST (faster,
  bigger; build.h) is off by construction, and parse.c's find_func()
  is its example. Nothing to do.

**A2. The function tables (1,960 of the 3,172 data bytes).** MAX_FUNCS
is 280 (254 user functions, obj.h). Each declared function costs
about seven data bytes (the five tables above). The language limit is
a feature: a program of 254 functions would not fit a smaller table.
If the self-hosting target's programs realistically use a few dozen
functions, dropping the limit to, say, 96 saves about 1,100 data
bytes (plus RLD and the limits' checks). It is the single biggest
data lever, at the price of the limit; decide it with the limit's
other users (MAX_VARS, MAX_FUNC_CODE sit beside it).

**A3. Messages.** The messages are one pool (ccish.msg, 303 of
them); cccomp references 123 distinct ones (its call sites number
them 19 to 232, parse.c alone has 138). Their text is cccomp's
strings section (554) plus one LA of the message per call site in
the code (and so one RLD entry per site). Merging messages that
share a form ("too many X" appears for functions, variables, enum
constants, types, tags and members) into one message with a what-
argument would save string bytes and call sites alike; the messages
are also what a user sees, so the merging has to keep them
readable. This is a cccomp, ccpp and ccgen matter (they share the
pool), not only cccomp's.

**A4. Runtime frames, not code size, but memory.** measure.py's stack
report for cccomp: worst case 538 bytes of frame stack, the largest
frames stop (208) and report (136), both in diag.c's error path. The
error path runs rarely, but its frames are reserved (the compiler
allocates for the worst case). Making stop() and report() shallower
(making them leaf-ish, or passing their buffers by reference) would
cut the reserved stack, not the module file. Worth doing for the
Apple II's 512-byte zero page and stacks, not for the 40K.

**A5. Not levers, checked:** a table instead of binary_prec's switch
would be about twice as big (the tokens are sparse); the id and type
tables are sized by the limits like the function tables; the input
and output buffers are the stream's width, not slack.

## 3. Making the code ccgen generates smaller

The peephole (peep.c, stage 3) is the right place for most of this:
it sees a ten-op window of the VM code after lowering and before
emission, it has the helpers expr_len(), store_len() and
stack_effect() for counting values, and every pattern is written to
be valid for C (the file's comment lists them). The levers below are
new patterns or analyses there, or in its neighbours. Each should be
measured with `tools/opcount.py` (ops, test by test) and, for byte
savings, on the test modules' sizes; the tests' generated code
together is the sample.

**B1. Dead stores, not just at the end of the function.**
dead_stores() removes stores to locals that nothing reads back, but
only at a LEAVE or RET, with nothing between but stack ops. The same
fact is true at any point: a store to a local that is not read again
is dead. Measured on the final listings (a scratch scan of every
run test, after the peep has run): of 897 frame stores, 135 are
never read again and never addressed (no LLA of the slot anywhere in
the function -- that is the escape that makes a later read through a
pointer untrackable). Under the coarser rule this doc first stated
(a call also stops the scan) only 16 qualify: the refined rule
needs no call barrier at all, because without an LLA a call cannot
see the slot (a value passed to it is a read; an address would be an
LLA). The dead stores cluster in dead initializations (an
`int a[7] = {1, ...}` never read: a CN then a SLW, 25 of them) and
in x++ forms (an LLW, INCR, store, 20+). The implementation, all in
ccgen (section 5's policy): the peep defers each plain store (SLW,
SLB -- not the keep-value DLW / DLB, whose kept copy flows into the
stack that follows, which would need value tracking) as a candidate
for its slot, with the side-effect-free expression that feeds it as
the span (the peep's own frame_expr and drop_dup shapes, seen at the
window's end when the store arrives); the candidate is decided dead
when the function ends (a read of the slot, an LLA of it, or a new
store to it keeps it -- all conservative); at the function's end the
peep hands emit the dead spans, and emit, where it already edits the
finished buffer (drop_frame), deletes or replaces them and re-patches
every branch offset word around the deletions (the relocs' offsets
move too; the relocs' targets are absolute, filled by ccrel, and
unaffected). A store replaced by a DROP is one byte; a fused span
(const or frame-only expression plus the store) is two to five.

**B2. Constant folding the peep doesn't. -- Done (33a4082b).**
fold_pair() in peep.c folds a constant against a constant for ADD,
SUB, MUL, DIV, MOD, AND, IOR, XOR, SHL, SHR and the six comparisons,
with the VM's 16-bit two's-complement semantics checked against both
VM implementations (plvm.c; plvm02.s's DIV is magnitude-then-sign,
C's toward-zero, and its SHR rounds toward minus infinity). Not
folded: a zero divisor (the VMs disagree, and C is undefined) and a
shift by sixteen or more. The literals stay 16 bits for the small
build's compiler (parse.c's wrap16 style). Measured: 4 of the 67
run tests fold something (an AND, a XOR, two comparisons; 16 ops and
28 bytes of module, opcount.py), 0 output differences. Cost: +528
code bytes in ccgen-small -- the small compiler's if-chain switch;
this doc's "a few bytes" estimate was low, and accepted under the
cost policy (ccgen may grow).

**B3. Leaf inlining, at the IR.** The biggest single lever and the
biggest project: irin.c holds every function's IR at once, so a call
to a leaf function (no calls of its own, a few ops, no recursion)
could be replaced by its body, labels remapped, locals merged into
the caller's frame. CALL, ENTER and LEAVE together are 17.5% of the
ops in the profile; a program of small helpers would shrink and
speed up a lot. The costs are real: recursion detection (a function
that calls itself, directly or through a cycle), frame growth (the
inlined function's locals become the caller's), code growth when the
leaf is called often (inlining copies; a threshold on the body's
size is needed), and the IR's label numbers are per-function (the
inlined body's labels must be renumbered into the caller's). Do it
only after B1, which is smaller and independent, with the op count
as the measured goal (a test by test `opcount.py` before and after).

**B4. Small switches as comparisons. -- Checked, not a lever.**
The arithmetic holds (one case: the compare chain is two bytes less
than SEL plus a one-entry table, five less when there is a default;
zero cases -- a switch with only a default -- is up to eleven less):
but the case count is known only after the body is parsed, and the
dispatch (the SEL) is emitted before it. So the threshold is not at
parse.c: it needs a tokenizer pre-scan of the body (save/restore of
tokin's position, a mechanism cccomp does not have; roughly three
hundred bytes of cccomp, the 40K-budget program) or a mid-function
rewrite in emit.c's finished buffer, with every branch offset
re-patched around the edit. The corpus has five one-case switches
and one zero-case one (counted_ret.c and switch.c): about 24 bytes.
Recorded per the cost policy; not built.



**B5. Not levers, checked:** a redundant ANDI 1 after a comparison
before a branch: the pattern does not exist -- comparisons (ISEQ
and kin) are lowered without any mask (lower.c's L_COMPARE adds the
NEG that makes C's 1; the 0xFFFF/0 is the known VM looseness), and
!x feeding a branch is already peeped (NOT ANDI 1 BRFLS/BRTRU ->
BRTRU/BRFLS; 5 of the 11 NOTs in the corpus); the only gap is a
double negation, once in the corpus (3 bytes). Common equivalent
tails: the IR-level duplicate arms do exist (22 pairs in the corpus,
all the CONST-then-JUMP arms of && and ||), but the machine code
already merges them: IR JOIN and emit.c's join_labels move the
branches so far to the earlier label, so one CONST serves them all
(verified in the listings); and a textual merge of longer tails
beyond adjacent labels is provably zero (a tail from an earlier point
is strictly longer, so two identical ones cannot end together).
Checked too: a return is already direct (IR_RETURN
lowers to LEAVE with the result on the evaluation stack, lower.c:
`return f(x);` is the call's args, CALL, LEAVE, with no store of the
result in between); the counted loops already use all four
step-and-branch ops (INCBRLE, ADDBRLE, DECBRGE, SUBBRGE, irin.c's
loop code); MUL is 1.0% of the profile's ops but every one is a
value times a value (the VM has no multiply op, and the peep's
constant-multiplier rewrites are done), so there is no size lever
there, only a slow one; the XORs (1.2%) come from user `^` code, not
from the compiler; the branch forms are already the tight ones
(BREQ/BRNE/BRFLS/BRTRU, with the peep's compare-and-branch
rewrites); and the two-op peephole window (a, b) would not see the
three-op `const op const` of B2 without a look back one, which
rewrite() already does in two places.

## 4. The profile the levers aim at

A one-off run (a copy of opcount.py's counting plvm, the same
mechanics) compiled and ran the 65 run tests that compile, under this
tree's ccish: 37,321 ops in all. The top:

| % | Ops | Op | Note |
|---|---|---|---|
| 17.5 | 6,520 | LLW  | local loads |
| 10.3 | 3,848 | CALL | calls |
|  5.1 | 1,908 | DLW  | keep-value stores |
|  4.6 | 1,709 | SLW  | plain stores |
|  3.6 | 1,332 | ENTER| frames |
|  3.6 | 1,332 | LEAVE| frames |
|  3.5 | 1,316 | CB   | byte constants |
|  3.1 | 1,170 | ADDLW| fused local adds (the peephole's) |
|  3.0 | 1,132 | INCBRLE | counted loops |
|  2.6 |   972 | IDXLW| fused index adds |
|  2.5 |   924 | LW   | loads through the stack |
|  2.4 |   887 | LAW  | absolute word loads |
|  2.3 |   853 | ADD  | |
|  2.3 |   852 | BRTRU| |
|  2.3 |   850 | INCR | |
|  2.2 |   810 | CW   | word constants |
|  2.1 |   802 | CN   | constants 0-15 |
|  2.0 |   747 | BRFLS| |
|  1.9 |   701 | LA   | addresses |
|  1.7 |   651 | $02  | CN 2 (the CN range is $00-$1E) |
|  1.7 |   616 | ISLT | |
|  1.2 |   430 | XOR  | user code's `^` |
|  1.1 |   412 | ISGE | |
|  1.1 |   392 | BRNCH| |
|  1.0 |   365 | MUL  | value times value |

Local-variable traffic (LLW, DLW, SLW and the fused ADDLW/IDXLW
forms) is about 33%; calls and frames (CALL, ENTER, LEAVE) about
17.5%; constants (CN, the CN range, CB, CW) about 11%. B1 aims at
the stores of the first group and B3 at the second.

## 5. Suggested order, and where the work is

1. B2 (const op const in the peephole): **done** (33a4082b).
2. B4 (one-case switches as comparisons): **checked, not a lever**
   (the case count is known only after the body; the cost lands in
   cccomp or in a risky mid-function emit rewrite; about 24 bytes in
   the corpus).
3. B1 (dead stores, the refined rule of section 3): in progress,
   in ccgen (the peep's deferred candidates plus an edit at the
   function's end in emit, with the branch offsets re-patched).
4. A2 (MAX_FUNCS, if the limit may drop): about 1,100 data bytes of
   cccomp plus its RLD, for a lower limit on functions per program.
   cccomp's own data, not the generated code's: a project decision
   on the limit, not a build.
5. A1 (the parser's shape): the biggest code win and the hardest
   review; take it one function at a time, measured. A1's ideas
   that would grow cccomp by more than a few bytes (such as a
   tokenizer pre-scan, B4's rejected mechanism) go to section 6
   first.
6. B3 (leaf inlining): a project with its own design doc (recursion,
   the label remap, the size threshold), after 1-4 have settled how
   the IR and the window are to be read.
7. A3 (message merging) and A4 (the error path's frames): when the
   others are done, or not at all; they buy the least.

## 6. Levers that cost cccomp: recorded, for later

Per the cost policy, these are real but not built, because each
needs a significant addition to cccomp (the 40K-budget program), not
ccgen:

- **B4's mechanism** (a switch body's case count before the
  dispatch): a tokenizer save/restore (a Bfile seek or a shadow
  buffer in bio/tokin) plus a brace-matched token walk in parse.c.
  About three hundred bytes of cccomp, for about 24 bytes in the
  corpus and a bit more in user programs (one-case switches with a
  default are the common shape).
- **A2's neighbours**: dropping MAX_VARS / MAX_FUNC_CODE with
  MAX_FUNCS (A2 itself) would save cccomp data, but changes the
  language's limits -- a decision, not a build.

And one lever that costs neither: section 7.

## 7. The VM's load: stream the RLD, don't hold it

The binding limit is the load: the VM reads a whole module file into
memory before running it (AGENTS.md, "The goal"), and cccomp's file
is 42,613 against about 40K. Of that, 12,398 is the RLD and ESD the
linker appends -- the four-byte relocation entries and the export
symbol directory, freed after linking but present during the load.
The C VM's loader (src/vmsrc/cmdsys.c's load_mod) reads the whole
remainder of the file into one heap block and then walks the RLD in
place; the 6502 hardware VMs load through their own loaders (the
Apple II's cmd.pla, the C64's), the same format.

The RLD needs no memory of its own: its entries are applied to the
code and data, which must be resident anyway. A streaming loader
would (1) read the header (it already does: 128 bytes), from which
the RLD's file offset is computable (the header's length plus
modsize); (2) allocate and read only the code and data; (3) walk the
RLD four bytes at a time from the file, applying each entry -- the
existing loop, with a pointer in the heap changed to a four-byte
buffer; the order of the entries does not matter, since the code and
data are all in memory by then; (4) hold only the ESD, in a small
buffer (a few hundred bytes), because lookup_extern() searches it
while the RLD is applied. Peak memory then drops from the whole file
to code plus data plus the ESD: for cccomp, 42,613 to about 30.2K --
under the 40K limit without changing cccomp's size at all, and the
biggest single lever this analysis has found.

It is a VM change, not a compiler change, and it is moderate: the
fixup logic is unchanged, only where its entries come from; the
re-entrancy (a module's dependencies load before its own bytes are
allocated) and the 128-byte-header assumption stay as they are. The
hardware loaders are separate work: the Apple II's ProDOS seeks, so
the same two-pass shape works; the C64's sequential channel prefers
a one-pass loader, which the format could give by writing the ESD
before the RLD (a ccrel change, a module-format version, and every
VM's loader updated together). Risk is low: a module that loads and
runs is a module that loads and runs, and the tests run every module
they build.

Each step: the worktree of the commit before it, `tools/fsize.py -c`
on the measure.py listings for the size, `tools/opcount.py` for the
generated code (expecting zero changes for anything that is not
itself a code-generation change), `./tests/run.sh` for the rest, and
the small build's byte-identical objects checked by the tests.
