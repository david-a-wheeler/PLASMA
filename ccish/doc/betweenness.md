# Efficiently computing betweenness

In many situations we must determine if an integer
is between two constants (inclusively) as part of a conditional.
Thus, it's important to do this efficiently for any two constants.

In C, the "obvious" way to determine if a value `x` is
between constants C1 and C2 is to compute: `(x >= C1) && (x <= C2)`
However, this translates to loading two constants, two comparisons,
and computing an "and" which involves branching. That ends up being a lot of code
on small systems. This page is about doing better, especially with this virtual machine.

We'll measure the bytes taken in the object code, as well as the average time in cycles
(when x is < C1, between C1..C2, and more that C2).
Often C1 and C1 will be between 0..255 inclusive, and x will be 1 char.
Sometimes x will be an integer, and the constants could be
small negatives or slightly larger integers (say, -255..1023).
We often know when x is a char and the constants within a char's range;
it's worth having a special optimization for that case.

## Obvious solution

The obvious solution is `(x >= C1) && (x <= C2)`.
Let's imagine this is part of a conditional:

~~~~
if {(x >= c1) && x <= c2)) {
  foo();
}
~~~~

Here are the optimized PLASMA instructions for a few cases, along with sizes and timings.

For `int x` with `c1 == 10`, `c2 == 50` (the `char x` case is identical
except `LLB` replaces `LLW`):

~~~~
LLW 0
CN 10
ISGE
BRFLS 1
LLW 0
CB 50
ISLE
BRFLS 1
; true:
CALL foo
~~~~

| x type | bytes | x &lt; c1 | c1 ≤ x ≤ c2 | x &gt; c2 |
|--------|:-----:|:--------:|:-----------:|:--------:|
| int    | 14    | 223      | 346         | 419      |
| char   | 14    | 218      | 336         | 409      |

`x &lt; c1` is cheapest: the first compare fails and the short-circuit bails
before the second load. The in-range and `x &gt; c2` cases both pay for two
loads and two compares; `x &gt; c2` is the worst because it also takes the
second branch.

## Standard solution

"Hacker's Delight" pages 51-52 provides a good solution if the system has an
efficient unsigned comparison operator. It exploits the fact that you can subtract the
first constant, causing numbers less than zero to "wrap around" and be interpreted as
huge unsigned integers.

A C macro implementation is:

~~~~
#define IS_BETWEEN_U(x, c1, c2) \
  (((unsigned)(x) - (unsigned)(c1)) <= ((unsigned)(c2) - (unsigned)(c1)))
~~~~

Everything is done in unsigned arithmetic, so the subtraction wraps instead of
overflowing and there is no signed-overflow undefined behavior; it is well
defined for any int width. In C, unsigned integers are guaranteed to wrap.
Subtracting `c1` from `x` leaves a small value when
`x >= c1` and a huge one when `x < c1`, so a single unsigned `<=` against the
small constant `c2 - c1` yields exactly `c1 <= x <= c2`. It works on ccish and
on ordinary C compilers alike.

However, in our VM only signed integer comparisons are built in. ccish emulates
the unsigned `<=` by flipping both sign bits and using a signed `<=`, which
costs more than the native signed comparison that the "General approach"
section below turns into `IS_BETWEEN`. We could do it with a call to the
CMDSYS `isule` routine, and the next section quantifies that call form.
On a host compiler the unsigned `<=` is a single native instruction, but not in
our VM (PLASMA) at this time.

Here is the optimized code for now, with sizes and timings, for the same
"if" conditional cases.

For `int x` with `c1 == 10`, `c2 == 50`. ccish emulates the unsigned `<=`
by flipping both sign bits (`XOR $8000`) and using a signed `<=`:

~~~~
LLW 0
SUBI 10
CW 32768        ; $8000
XOR
CW 32808        ; (50-10) ^ $8000
ISLE
BRFLS 0
; true:
CALL foo
~~~~

| x type | bytes | x &lt; c1 | c1 ≤ x ≤ c2 | x &gt; c2 |
|--------|:-----:|:--------:|:-----------:|:--------:|
| int    | 14    | 374      | 295         | 368      |

The sign-bit flip costs two extra ops (`CW`+`XOR`) over a native unsigned
compare, which is why this is bigger and slower than `IS_BETWEEN` below.

Here are the presumed code and timings if we added unsigned comparisons to
the VM instruction set:

If the VM had a native unsigned compare, the sign-bit flip disappears and
`c2 - c1` (a small constant) is compared directly:

~~~~
LLW 0
SUBI 10
CB 40           ; c2 - c1
ISULE           ; unsigned <= (new op)
BRFLS 0
; true:
CALL foo
~~~~

| x type | bytes | x &lt; c1 | c1 ≤ x ≤ c2 | x &gt; c2 |
|--------|:-----:|:--------:|:-----------:|:--------:|
| int    | 9     | 275      | 196         | 269      |

A native unsigned `<=` would make this the smallest general form (9 bytes)
and beat `IS_BETWEEN` on time in every case.

## IS_BETWEEN_U via a CMDSYS call

The VM has no unsigned compare opcode, but every PLASMA target's CMDSYS
library exports one as a callable function: `isule` (also `isult`, `isugt`,
`isuge`), `int, int -> int`, leaving `$FFFF` (true) or `0` (false) on the
stack, the same shape the VM's own comparisons produce, so `BRFLS`/`BRTRU`
work on the result without change. The call form of `IS_BETWEEN_U` is
therefore available today, with no compiler change.

For `int x` with `c1 == 10`, `c2 == 50` (`char x` is identical except
`LLB` replaces `LLW`):

~~~~
LLW 0
SUBI 10
CB 40           ; c2 - c1
CALL isule      ; 3 bytes: the import, relocated at load
BRFLS 0
; true:
CALL foo
~~~~

| x type | bytes | x &lt; c1 | c1 ≤ x ≤ c2 | x &gt; c2 |
|--------|:-----:|:--------:|:-----------:|:--------:|
| int    | 11    | 343      | 262         | 337      |
| char   | 11    | 338      | 257         | 332      |

The per-op costs are the ones from `Bytecodes-cycles.md`: `LLW` 59 / `LLB`
54, `SUBI` 41 (47 when `x < c1` borrows), `CB` 41, the `CALL isule`
pseudo-op 121 (83 for the `CALL`, trampoline included, plus the 38-cycle
`uword_isle` body in the library, whose `RTS` returns into the handler),
and `BRFLS` 75 counted only when taken, as in the other rows on this page.
The body is unconditional, so the call costs the same whether the result
is true or false; both `x < c1` and `x > c2` take the final branch.

The bytes follow the page's convention: every op at full size, the final
branch counted as 2. The in-range figures do not count the final `BRFLS`'
not-taken cost, as in every int row on this page; the byte rows' in-range
figures do include their final `BRNE`'s not-taken cost (55 cycles), a
pre-existing inconsistency in the table.

Averages, with the page's workload (8 of 256 values in, 124 below, 124
above): int (8*262 + 124*343 + 124*337)/256 = 86416/256 = 337.6; char
(8*257 + 124*338 + 124*332)/256 = 85136/256 = 332.6.

### What it costs, and what it replaces

* **No new declarations.** `isule` (and the other three) are already
  CMDSYS names: a call auto-declares them from the library's signature
  (`int (int, int)`), or a program may declare
  `extern int isule(int, int);`, which is checked against it (a mismatch
  is an error).
* **No new link machinery.** The import/ESD/CMDSYS-dependency path exists
  and is already used by every module (the init function the compiler
  generates calls `isult` itself); the loader resolves the import against
  the library's exports at load.
* **It replaces the current unsigned lowering.** Today
  `(unsigned)x >= c1 && (unsigned)x <= c2` compiles to the naive two
  sign-flipped comparisons (25 bytes; 327/549/622 for the word case, where
  the `x < c1` path bails after the first compare, so it is 16 cycles
  *faster* than the call form on that one case; the in-range path takes no
  final branch and `x > c2` takes it), and the Hacker's Delight macro
  `IS_BETWEEN_U` compiles to the 14-byte sign-flipped form (374/295/368).
  The call form is 11 bytes and 343/262/337: smaller than both, faster
  than both on average (337.6 against 476.8 and 368.6), and faster in the
  in-range and above-range cases; it loses only the naive form's early-out
  `x < c1` case.
* **The C macro needs no compiler change.** Replacing the emulated
  unsigned `<=` with the call,
  `#define IS_BETWEEN_U(x, c1, c2) isule((x) - (c1), (c2) - (c1))`,
  compiles today to the op stream above. This was checked with ccish and
  run under `plvm`: `isule(x - 10, 40)` agreed with
  `(unsigned)x >= 10 && (unsigned)x <= 50` for every `x` in 0..80 and in
  65525..65535 (the unsigned top, where `x - c1` wraps and must still read
  as above `c2 - c1`).
* **The IR rewrite is the optional follow-up.** As with the signed case
  (which the IR pass already rewrites), the pass could recognize the
  unsigned shape (load, sign-bit flip, constant, ISGE/ISLE, second load,
  flip, constant, ISLE/ISGE, both branches to the same label) and emit
  `LOAD, SUB c1, CONST c2-c1, CALL isule, BRFALSE`. The macro gives the
  same op stream with zero compiler code, so the rewrite is not needed for
  correctness or for the win; it is only automaticity.
* **The trade against a native op.** The hypothetical native `ISULE`
  (9 bytes, 275/196/269) would still beat the call form: the call pays the
  83 cycles of `CALL` machinery and 2 extra bytes. The call form is the
  best general shape the system can do today, and it is the demonstration
  case for adding the unsigned compare opcode.

## Why the XOR sign-bit flip is sound

ccish has no native unsigned compare. To evaluate `a <= b` with `a` and `b`
unsigned, irin.c flips the sign bit of *both* operands (XOR `$8000`) and
reuses the signed `ISLE`. The left operand's flip is a runtime `CW 32768` /
`XOR`; the right operand's is folded into its constant when it is one (XOR the
constant), or a second flip when it is a computed value. For example,
`(unsigned)x <= 0x8000` lowers to `x; CW 32768; XOR; CN 0; ISLE`, the signed
test `(x ^ $8000) <= 0`; and `(unsigned)x <= 0x7FFF` lowers to
`x; CW 32768; XOR; MINUS1; ISLE`, the signed test `(x ^ $8000) <= -1`.

This is always correct, for every 16-bit value, including `$8000` (the signed
`-32768`, the edge the question worries about). The reason is that XOR with
`$8000` is an order-preserving bijection from the unsigned order to the signed
order:

| unsigned `v` | `v ^ $8000`, read as signed |
|--------------|-----------------------------|
| 0            | -32768                      |
| 1            | -32767                      |
| 2            | -32766                      |
| 3            | -32765                      |
| ...          | ...                         |
| 32767 ($7FFF)| -1                          |
| 32768 ($8000)| 0                           |
| ...          | ...                         |
| 65535 ($FFFF)| 32767                       |

As `v` increases through 0 to 65535, `v ^ $8000`, read as a signed value,
increases through -32768 to 32767. The mapping never decreases, so for any
unsigned `a` and `b`:

    a <= b  (unsigned)    if and only if    (a ^ $8000) <= (b ^ $8000)  (signed)

The sign bit is exactly the boundary between the two halves of the unsigned
range. Flipping it relocates the upper unsigned half (32768 to 65535) to the
top of the signed range (0 to 32767) and the lower half (0 to 32767) to the
bottom (-32768 to -1), preserving each half's internal order and their order
relative to each other. There is no wraparound or overflow in the XOR (it is a
bitwise op, not an arithmetic one), so no edge value, including `$8000`
itself, breaks it: `$8000 ^ $8000 == 0`, the signed zero, which sorts exactly
where unsigned 32768 belongs, just above unsigned 32767's image `-1` and just
below unsigned 32769's image `1`.

This was checked directly, with ccish and gcc agreeing on the boundary cases
`32767 <= 32768` (true), `32768 <= 32767` (false), `32768 <= 32768` (true),
`65535 <= 0` (false), and `0 <= 65535` (true).

## General approach if unsigned ops are not efficient

We'll presume we're using 2's complement signed integer arithmetic and C1 <= C2.
We'll efficiently compute BETWEEN(x, low, high) == `(x >= C1) && (x <= C2)`,
but *avoid* using unsigned comparison.

Interestingly, I had an AI work for a *long* time on this, and it didn't come up
with anything. I did. So hooray for team humans I guess.

### Replacing unsigned comparison

Instead subtracting by C1 to force wrapping around 0 of values we don't want,
we'll add enough to force wrapping of unwanted numbers
around `MAX_INT`. Since we don't know if `MAX_INT` will be defined, we will
define our own `MY_MAX_INT` == `(int)((~0U) >> 1)`.

Now let's compute this constant:

xlate == (`MY_MAX_INT` - C2)

Let's start with what we want, and translate it:

* `(x >= C1) && (x <= C2)` ; original form
* `(x + xlate >= C1 + xlate) && (x + xlate <= C2 + xlate)` ; moved to the right.

However, `C2 + xlate` == `C2 + (MY_MAX_INT - C2)` == `MY_MAX_INT`.
Now, here's the trick: by definition, *all* integers are equal to or less than the
maximum integer `MY_MAX_INT`. Thus `(x + xlate <= C2 + xlate)` is always true, and we
can throw away that expression since `and true` is unnecessary. That leaves only the
left-hand-side `(x + xlate >= C1 + xlate)`.

Let's take the left-hand-side simplify its right-hand-side
`C1 + xlate)`, which is `C1 + (MY_MAX_INT - C2)`.
That simplifies to `MY_MAX_INT - C2 + C1`, a *constant* expression.

Final result is that this simplifies to
`(x + xlate >= (MY_MAX_INT - C2 + C1))`; since the right-hand-side is constant,
this becomes *one* addition, two load constants, and one comparison `>=`.

We need to do some extra work in C to ensure the types are always correct,
but that's okay, none of that costs execution time.

### C code

The proposed C code is:

~~~~
const int MY_MAX_INT = (int) (~0U >> 1);
#define IS_BETWEEN(x, c1, c2) ((int) (x) + (MY_MAX_INT - c2) >= (MY_MAX_INT - c2 + c1))
~~~~

Note that `MY_MAX_INT` could also be a #define; it's shown as a constant here so
that the compiler front end doesn't need to recompute and re-fold the constant every time
`IS_BETWEEN` is used.

### PLASMA code, size, timings

For `int x` with `c1 == 10`, `c2 == 50` (`char x` is identical except
`LLB` replaces `LLW`). The front end folds `MY_MAX_INT - c2` to `32717`
and `MY_MAX_INT - c2 + c1` to `32727`:

~~~~
LLW 0
CW 32717
ADD
CW 32727
ISGE
BRFLS 0
; true:
CALL foo
~~~~

| x type | bytes | x &lt; c1 | c1 ≤ x ≤ c2 | x &gt; c2 |
|--------|:-----:|:--------:|:-----------:|:--------:|
| int    | 12    | 329      | 256         | 329      |
| char   | 12    | 324      | 251         | 324      |

One addition, two constants, one compare. `x &lt; c1` and `x &gt; c2` cost the
same (both fail the single compare and take the branch); the in-range case
is the cheapest of all the general forms because it takes no branch.

## Specialized byte optimization using AND

When `x` is a byte (0 to 255), or an `int` that the program's logic is known to keep
within 0 to 255 (inclusive), and the range `[c1, c2]` is an *aligned power-of-two block*,
the betweenness collapses to one bitwise test that ccish lowers to a single cheap
`ANDI` followed by a comparison to a constant.

I have done this by hand for years, as have many others. This is standard
bit operations work. I also knew that Hacker's Delight
had general algorithms for computing the values. An AI worked out the details,
and here they are (after human review).

### When it applies

Let the width be `w == c2 - c1 + 1`. The range is one aligned block of width
`w` exactly when:

1. `w` is a power of two, and
2. `c1` is a multiple of `w` (its low `log2(w)` bits are all zero).

Condition 2 is easy to get wrong: **`c1` need not itself be a power of two.**
It only needs a run of zero bits at its low end, long enough to hold the block
that we're going to "AND" out.

For example,
`c1 == 48` (binary `110000`) is not a power of two, but its low four bits are
zero, so it starts the width-16 block `[48, 63]`. The widest block a given `c1`
can start is `2` raised to its number of trailing zero bits. Once `w` and `c1`
are set, `c2 == c1 + w - 1` has its low `log2(w)` bits all one automatically,
so the "c2's low bits are 1" half needs no separate test.

### Why it works

Let `k == log2(w)`, so `w == 2^k`, and let the byte mask `M == 256 - w` clear
the low `k` bits and keep the high `8 - k` bits (as a byte, `M` is `~(w - 1)`).

Take any `x` in the block, written `x == c1 + i` with `0 <= i < w`. Because
`c1`'s low `k` bits are zero and `i < 2^k`, the sum `c1 + i` changes only the
low `k` bits; there is no carry into bit `k`. So the high `8 - k` bits of `x`
are exactly those of `c1`, and

~~~~
x & M == c1            (c1's low k bits are 0, so the mask keeps all of c1)
~~~~

Conversely, an `x` outside the block lies in a different `w`-aligned block, its
high `8 - k` bits differ from `c1`'s, and `x & M != c1`. Hence

~~~~
c1 <= x <= c2          if and only if          (x & M) == c1
~~~~

One `ANDI` with the byte mask `M` and one `ISEQ` against `c1`: no addition, no
wide constant, no branch.

### Why ccish makes it cheap

Two ccish behaviors turn the macro below into one `ANDI` per check:

* ccish **folds a `?:` whose condition is a compile-time constant**, keeping
  only the taken arm. `c1` and `c2` are always constants, so the "is this an
  aligned block?" test is decided at compile time and the other arm is never
  emitted.
* ccish lowers `(char)(x) & byte-constant` to `ANDI` even when `x` is an `int`.
  The `(char)` cast is what lets a value *known* to be 0 to 255 take the fast
  path. On a real `char` the cast is a no-op; on an `int` ccish emits an
  `ANDI 255` for the cast and then the mask `ANDI`, and its peephole merges
  the pair (`ANDI m ANDI n` becomes `ANDI (m & n)`, and `255 & n` is `n`),
  leaving a single mask `ANDI`.

The `(char)` cast also states the contract: "treat `x` as its low byte", which
is correct exactly when `x` is 0 to 255. Because this is decided by the macro,
it is a source-level fix: no change to the compiler is needed. (A compiler that
recognized the `x + C1 >= C2` IR pattern could do the same transform on its
own, but the macro already suffices.)

### The macro

Under the contract that `x` is a byte (or an `int` kept in 0 to 255) and
`c1`, `c2` are byte bounds with `c1 <= c2`, the width `w` is always in 1 to
256, so `IS_POW_2` needs no explicit bounds and `BB_M(w)` is always a valid
byte mask:

~~~~
const int MY_MAX_INT = (int) (~0U >> 1);
#define IS_BETWEEN(x, c1, c2) ((int) (x) + (MY_MAX_INT - c2) >= (MY_MAX_INT - c2 + c1))

#define IS_POW_2(w)    (((w) & ((w) - 1)) == 0)       // is w a power of two?
#define IS_ALIGNED(c1,w)   (((c1) & ((w) - 1)) == 0)  // is c1 aligned to w?

#define BB_W(c1,c2)   ((c2) - (c1) + 1)               // # of distinct values in [c1,c2]
#define BB_M(w)       (256 - (w))                     // the byte ANDI mask

// Is byte x between constants c1 and c2? Use AND if you can
#define IS_BYTE_BETWEEN(x, c1, c2) \
  ( IS_POW_2(BB_W(c1,c2)) && IS_ALIGNED(c1, BB_W(c1,c2)) \
    ? (((char)(x)) & BB_M(BB_W(c1,c2))) == (c1) \
    : IS_BETWEEN(x, c1, c2)
~~~~

Notes:

* The macro is split into short helper lines because a single `#define` body is
  limited to a 255-character logical line in ccish.
* Cost of the chosen arm (the comparison, before the result is boolified):
  `ANDI` + `CN`/`CB` + `ISEQ` is 4–5 bytes whether `x` is a `char` or an `int`
  (an `int`'s `ANDI 255` cast folds into the mask); the general form is 8.
* A singleton `c1 == c2` (width 1) and the whole byte (width 256, always true)
  are both handled. On an `int`, a singleton's cast and width-1 mask are both
  `ANDI 255`, which the peephole merges to one, and the whole-byte case folds
  to the tautology `ANDI 0, CN 0, ISEQ`; both are still smaller than the
  general form.
* For a true word value (an `int` not known to be 0 to 255) `ANDI` cannot be
  used, because the block mask has high bits set; the full-word `CW`+`AND` form
  is only marginally smaller than, and no cheaper than, the general
  `IS_BETWEEN`. The byte case is the one that pays.
* This presumes `//` is supported for comments; ccish does.

### PLASMA code, size, timings

For `char x` with an aligned power-of-two block, `c1 == 48`, `c2 == 63`
(width 16, mask `256 - 16 == 240`). The `== c1` fuses with the branch into
a single `BRNE`:

~~~~
LLB 0
ANDI 240
CB 48
BRNE 0
; true:
CALL foo
~~~~

| x type | bytes | x &lt; c1 | c1 ≤ x ≤ c2 | x &gt; c2 |
|--------|:-----:|:--------:|:-----------:|:--------:|
| char   | 8     | 216      | 193         | 216      |

One `ANDI` and one `BRNE`: the smallest and fastest form on this page.
`x &lt; c1` and `x &gt; c2` are the same cost (both fall outside the block).

## Extending the macro for unaligned values

The aligned form above needs `c1` to be a multiple of the width `w`.
If `w` is a power of two but `c1` is *not* aligned to it, we can still use
`ANDI` by first shifting `x` back onto an aligned block.

Let the misalignment be `r == c1 mod w`, the low `log2(w)` bits of `c1`
(equivalently `c1 & (w - 1)`). Subtracting `r` from both `x` and `c1` moves
the range onto the aligned block of width `w` that starts at the multiple of
`w` just below `c1`, namely `c1 - r`. The same masking argument as above now
applies to `x - r` instead of `x`, with the aligned base `c1 - r`:

~~~~
c1 <= x <= c2          if and only if          ((x - r) & M) == (c1 - r)
~~~~

Because `c1 - r` is a multiple of `w` by construction, its low `log2(w)` bits
are zero, and the mask `M == 256 - w` keeps exactly the high bits that
identify the block. When `c1` *is* aligned, `r == 0` and `x - r` is just `x`,
so this collapses to the aligned macro above (ccish folds the `x - 0`). So a
power-of-two width is always usable, aligned or not; only the misalignment
`r` changes.

The two new helpers, in the same style, are:

~~~~
#define BB_R(c1,w)     ((c1) & ((w) - 1))      // misalignment, c1 mod w
#define BB_K(c1,w)     ((c1) - BB_R(c1,w))     // aligned base, c1 - r
~~~~

and the macro generalizes by dropping the `IS_ALIGNED` test and feeding
`x - r` and the aligned base to the same `ANDI`:

~~~~
#define IS_BYTE_BETWEEN(x, c1, c2) \
  ( IS_POW_2(BB_W(c1,c2)) \
    ? (((char)(x - BB_R(c1, BB_W(c1,c2)))) & BB_M(BB_W(c1,c2))) == BB_K(c1, BB_W(c1,c2)) \
    : IS_BETWEEN(x, c1, c2) )
~~~~

This is the more general of the two macros on this page: it is identical to
the aligned-only one when `c1` is aligned, and additionally handles unaligned
power-of-two widths. Use it when you need that generality. It costs more, in
a few ways that matter in a 64 K target:

* An extra `SUBI r` (two bytes) before the `ANDI`, but only in the unaligned
  case (`r != 0`). When `r == 0` ccish folds `x - 0` to `x`, so the aligned
  arm emits no `SUBI` at all.
* The `(char)(x - r)` cast emits an `ANDI 255` immediately before the mask
  `ANDI`, but the peephole merges that pair (`ANDI m ANDI n` to
  `ANDI (m & n)`, and `255 & n` is `n`), so the redundant `ANDI 255` never
  survives. The unaligned arm is then six to seven bytes, *cheaper* than the
  eight-byte `IS_BETWEEN`; the aligned arm is four to five.

Its expansion is also longer than the aligned macro's, because the width is
re-expanded inside `BB_R` and `BB_K`; it sits closer to ccish's 255-character
expansion limit, though the small numeric bounds the compiler actually uses
fit comfortably.

### PLASMA code, size, timings

For `char x` with an unaligned power-of-two block, `c1 == 50`, `c2 == 57`
(width 8, misalignment `r == 50 & 7 == 2`, aligned base `48`, mask
`256 - 8 == 248`):

~~~~
LLB 0
SUBI 2
ANDI 248
CB 48
BRNE 0
; true:
CALL foo
~~~~

| x type | bytes | x &lt; c1 | c1 ≤ x ≤ c2 | x &gt; c2 |
|--------|:-----:|:--------:|:-----------:|:--------:|
| char   | 10    | 257      | 234         | 257      |

The unaligned arm adds one `SUBI` over the aligned arm (2 bytes, ~41
cycles), but is still smaller and faster than the general `IS_BETWEEN`
(12 bytes, 251/324 cycles) for an `int`-sized value known to be a byte.

## Extended IS_BYTE_BETWEEN for int

Here is the analysis of the impact of extending IS_BYTE_BETWEEN for int
when the range's width, c2 - c1 + 1, is a power of two.

### When it applies

The byte form needs x to be 0 to 255. For a true int x, the same mask
form works whenever the width w == high - low + 1 (the inclusive ends the
comparisons describe: > c1 means low is c1 + 1, < c2 means high is c2 - 1)
is a power of two from 1 to 65536, with one check the byte case can skip:
low <= high *signed*, the range is not empty. The byte case can never be
empty (its low is required to be >= 0), but the word case can be: x > 32767
&& x <= 32767 is always false, yet its ends, low == 32768 and high ==
32767, describe the whole word as a mod-65536 interval (width "65536"),
which the mask form would emit as always true. Note that c1 == 32767 with >
makes low 32768, which overflows 16 bits, so w, r, base and M must be
computed with & 0xFFFF (unsigned 16-bit) arithmetic; after the non-empty
check low is at most 32767, so w is 1 to 65536 and fits in an int.

### Why it works

Same argument as the byte case, with a 16-bit mask. Let k == log2(w),
r == low mod w (== low & (w - 1), 16-bit), base == low - r (16-bit), and
M == 65536 - w. The mask keeps the high 16 - k bits: the block's low k bits
are free, so after shifting off the misalignment r, x is in the interval
exactly when the kept high bits are base's:

~~~~
low <= x <= high        if and only if        ((x - r) & M) == base
~~~~

all mod 65536. A signed inclusive interval with low <= high is exactly one
wrapping mod-65536 interval, so the mod arithmetic and the signed test
agree for every 16-bit x. This was checked exhaustively with a host
program, not by hand: every 16-bit x against every signed power-of-two
interval (about 64 billion checks); the two sides agreed everywhere.

Two examples, with the decomposition the rewrite computes:

| range | w | r | base | M |
|-------|:---------:|:---------:|:---------:|:---------:|
| x >= -8 && x <= 7 | 16 | 8 | 65520 | 65520 |
| x >= -16384 && x <= 16383 | 32768 | 16384 | 32768 | 32768 |

The first is the one to get right: -8's low four bits are 1000, so the
misalignment r is 8, not 0; the aligned base below the range is 65520,
not 65528. (With r of 0 and base 65528 the test (x & 65520) == 65528 could
never be true: the mask clears the low four bits, and 65528's are 1000.)

### Why the AND is longer than the byte ANDI

For a byte, the mask M == 256 - w is a byte, so the mask is one ANDI (two
bytes). For a word, the mask has 1 bits above the low eight: M == 65536 -
w is at least 0xFF00 for every w <= 256, so an ANDI could not keep those
high bits, and the mask is a word AND with a two- or three-byte constant:

* MINUS1 (one byte) for w == 1 (M is 0xFFFF);
* CFFB (two bytes) for 2 <= w <= 256 (M is 0xFF00 to 0xFFFE);
* CW (three bytes) for 512 <= w <= 32768 (M is 0x8000 to 0xFE00);
* CN 0 (one byte, meeting the AND as an ANDI 0 in the peephole) for the
  whole-word w == 65536 (M is 0).

The compare's constant is likewise often a CW (three) where a byte base
was a CB (two). That is the word form's extra one to two bytes over the
byte form; it is still one to six bytes under the two-comparison obvious
form, as the next section shows.

### The machine form, and its size

The rewrite emits one load, the misalignment r subtracted, the mask ANDed,
and one compare to the aligned base; the peephole stage makes it best: an r
of 0 drops the x - 0, an r of 1 is a DECR, an r of 2 to 255 is a SUBI, an r
above 255 (only for w >= 512) is a CW and a SUB, a constant AND becomes an
ANDI when the mask is 0 to 255 (only the whole-word case), and the compare's
BRFALSE meets the ISEQ as a BRNE. These are the verified listings (ccish
-d, the page's byte convention: every op at full size, the final branch
counted as 2), against the obvious two-comparison form:

| range | w, r | machine form | bytes | obvious | saved |
|-------|:----:|--------------|:-----:|:-------:|:-----:|
| [16, 31] | 16, 0 | LLW CFFB 240 AND CB 16 ISEQ BRNE | 10 | 15 | 5 |
| [17, 32] | 16, 1 | LLW DECR CFFB 240 AND CB 16 ISEQ BRNE | 11 | 15 | 4 |
| [10, 25] | 16, 10 | LLW SUBI 10 CFFB 240 AND CN 0 ISEQ BRNE | 11 | 14 | 3 |
| [-8, 7] | 16, 8 | LLW SUBI 8 CFFB 240 AND CFFB 240 ISEQ BRNE | 12 | 14 | 2 |
| [200, 200] | 1, 0 | LLW MINUS1 AND CB 200 ISEQ BRNE | 9 | 15 | 6 |
| [32752, 32767] | 16, 0 | LLW CFFB 240 AND CW 32752 ISEQ BRNE | 11 | 17 | 6 |
| [800, 1311] | 512, 288 | LLW CW 288 SUB CW 65024 AND CW 512 ISEQ BRNE | 16 | 17 | 1 |
| [-16384, 16383] | 32768, 16384 | LLW CW 16384 SUB CW 32768 AND CW 32768 ISEQ BRNE | 16 | 17 | 1 |
| [-32768, 32767] | 65536, 32768 | LLW CW 32768 SUB ANDI 0 CN 0 ISEQ BRNE | 12 | 17 | 5 |

Smaller in every case: five to six bytes for small aligned ranges, two to
four for unaligned narrow ones, one for the wide unaligned ones (where the
CW + SUB r eats the win). The whole-word row is a tautology, always true;
the rewrite emits the always-true mask form (x - r & 0 is 0), which the
peephole could fold further but does not, and it still saves five bytes.

### The machine form, and its time

Per-case cycle counts from the op costs in Bytecodes-cycles.md (LLW 59,
DECR 33, SUBI 41, CW 46, SUB 50, CFFB/CB 41, CN 36, MINUS1 34, AND 48,
ANDI 43, ISEQ true 54 / false 45 when the low bytes differ, 55 when only
the high bytes differ; BRNE not-taken 55, taken 78 / 88 the same way;
ISGE/ISLE 53 false / 55 true; BRFLS 41 not-taken / 75 taken). For w <= 256
the out-of-range value usually differs in the low byte (the 45/78 path); a
value that differs only in the high byte costs 20 more. For w >= 512 both
(x - r) & M and the base have a zero low byte (the mask clears at least the
low nine bits, and the base is a multiple of w), so the 55/88 path is the
only one, and the out-of-range cost is exactly flat.

| range | AND form: out of range | AND form: in range |
|-------|:----------------------:|:------------------:|
| [16, 31] | 312 | 298 |
| [17, 32] | 345 | 331 |
| [10, 25] | 348 | 334 |
| [-8, 7] | 353 | 339 |
| [800, 1311] | 438 | 404 |
| [-16384, 16383] | 438 | 404 |
| [-32768, 32767] | (all values in range) | 343 |

| range | obvious: x < c1 | obvious: in | obvious: x > c2 |
|-------|:---------------:|:-----------:|:---------------:|
| [16, 31] | 228 | 351 | 424 |
| [17, 32] | 228 | 351 | 424 |
| [10, 25] | 223 | 346 | 419 |
| [-8, 7] | 228 | 346 | 419 |
| [800, 1311] | 233 | 361 | 434 |
| [-16384, 16383] | 233 | 361 | 434 |
| [-32768, 32767] | (never) | 361 | (never) |

The AND form is flat: no early out, the same cost either side of the
range. The obvious form bails early below c1 (cheap) but pays full price
above c2 (two loads, two compares, a taken branch). Which wins on average
depends on where x usually is:

* **The page's estimator** (the 8/124/124 workload of "How the average is
  computed"), which for a narrow range is just the uniform 16-bit signed
  average (half the space is below c1, half above):

  | range | obvious | AND form |
  |-------|:-------:|:--------:|
  | [16, 31] | 326.8 | 311.6 |
  | [17, 32] | 326.8 | 344.6 |
  | [10, 25] | 321.8 | 347.6 |
  | [-8, 7] | 324.2 | 352.6 |
  | [800, 1311] | 334.4 | 436.9 |
  | [-16384, 16383] | 334.4 | 436.9 |
  | [-32768, 32767] | 361.0 | 343.0 |

* **x uniform over the full signed 16-bit space** (the natural model for a
  general int): the same picture, to within a cycle for the narrow ranges
  (aligned 312.0 vs 325.9; r 1 345.0 vs 325.9; r 10 348.0 vs 321.0;
  -8..7 353.0 vs 323.5; the wide rows 437.7/421.0 vs 330.5/347.2).

* **x usually a byte (0 to 255)** - this page's stated workload guess, and
  the one the narrow-range forms target (character classes, small-set
  tests). For a range in or near the byte space almost every byte value is
  *above* c2, where the obvious form is worst, and the AND form wins
  every narrow case by 60 to 96 cycles (311.1 vs 407.2 for [16, 31];
  344.1 vs 406.4 for [17, 32]; 347.1 vs 406.8 for [10, 25]; 352.6 vs
  416.7 for [-8, 7]).

The sums were done with a small program, not by hand.

### The verdict

* **Shorter, always.** One to six bytes under the obvious form for every
  power-of-two word range; five to six for small aligned ranges, two to
  four for unaligned narrow ones, one for the wide unaligned ones.
* **Faster on average when x is concentrated in the low part of the
  space** (the page's "x is usually a byte" guess): 60 to 96 cycles for
  every narrow range, aligned or not, negative or not.
* **Faster on average even for a uniform 16-bit x when the range is
  aligned** (15 cycles under the page's estimator, 14 under the uniform
  model) or is the whole word (18).
* **Slower on average for a uniform 16-bit x when the range is unaligned**
  (18 to 30 cycles: the misalignment subtract the obvious form avoids) or
  wide (74 to 107: the CW + SUB r, and the obvious form's early out covers
  a real fraction of the space). Those are also the ranges where the size
  win is smallest (one byte).

Since size is the live 64K constraint, the size win is unconditional, and
the time win holds for the narrow-range, low-x workload this page targets
and for aligned ranges under any workload, the net is a win: extend the
rewrite.

### Forcing it: the (char) cast on an int

A user can force the byte form on an int by casting: (char)x >= c1 &&
(char)x <= c2. The front end spells the (char) cast as the load, a CONST
255, and an AND, so the held tail is the twelve-op shape

~~~~
LOCAL n   CONST 255   AND   CONST c1   GE/GT   BRFALSE L
LOCAL n   CONST 255   AND   CONST c2   LE/LT   BRFALSE L
~~~~

which the rewrite also matches (the same int local loaded twice, each load
masked to its low byte, the same two comparisons, both fails to the same
label), when the range is a power-of-two block of bytes. It emits the
byte form on the word load, the same eight-op tail the char case emits
with the load left as the int's LOCAL: (x - r) & M with M and base below
256 keeps only the low byte of x - r, which is the low byte of (x & 255)
- r, so the test is exactly the cast's (checked exhaustively as above).
The explicit cast op never needs to be emitted: the mask AND already
clears the high byte. The machine form is the byte form with LLW for
LLB - the same size (both loads are two bytes), five cycles slower on the
load - and it is what the IS_BYTE_BETWEEN macro produces by hand today
(the macro's (char) cast emits an ANDI 255 that the peephole merges into
the mask ANDI). A (char) on a char local emits no cast at all (the value
is already a byte), so it takes the existing eight-op shape.

### How the rewrite changes

In between_rewrite() (irin.c), the match and the emit:

* the load is accepted as IR_LOCALB (the char case, unchanged) or IR_LOCAL
  (the word case), both loads the same local;
* the range check becomes: reject low > high (signed, the empty case);
  for a char local, keep 0 <= low <= high <= 255 (the block must be whole
  bytes); for a word local, w == high - low + 1 must be a power of two
  ((w & (w - 1)) == 0, with w in 1 to 65536 covering the whole word);
* r == (low & 0xFFFF) & (w - 1), M == (65536 - w) & 0xFFFF, base == (low -
  r) & 0xFFFF (the byte case's 256 - w and low - r are the same formulas
  with the 16-bit mask a no-op);
* the same eight-op tail is emitted, so nbuf is untouched and no peephole
  changes are needed (its existing folds do all the finishing, as listed
  in "The machine form, and its size");
* the twelve-op (char)-cast shape is matched as well, and its tail is
  replaced by the same eight-op byte form on the int's LOCAL, which is
  four ops shorter: the tail is rewritten in place, nbuf is set back four,
  the dropped BRFALSE's label count is decremented as in the eight-op
case, and the held buffer is lowered, as the truth runs' run_do does
  after a length-changing rewrite, so a later IR_SWAP whose counted block
  covered the tail falls back to the value exchange (which needs no
  lengths) instead of trusting the parser's stale count.

Everything downstream (lowering, peephole, truth runs, joins) is
unaffected: the byte form is the special case where M and base are below
256.

## Final comparison

All the forms on one table, with an added `average` column.

| Construct | bytes | x &lt; c1 | in | x &gt; c2 | average |
|-----------|:-----:|:--------:|:--:|:--------:|:-------:|
| Obvious `x >= c1 && x <= c2`, int      | 14 | 223 | 346 | 419 | 321.8 |
| Obvious `x >= c1 && x <= c2`, char     | 14 | 218 | 336 | 409 | 314.2 |
| `IS_BETWEEN`, int                      | 12 | 329 | 256 | 329 | 326.7 |
| `IS_BETWEEN`, char                     | 12 | 324 | 251 | 324 | 321.7 |
| `IS_BETWEEN_U` (signed-emulated), int  | 14 | 374 | 295 | 368 | 368.6 |
| `IS_BETWEEN_U` via CMDSYS call, int    | 11 | 343 | 262 | 337 | 337.6 |
| `IS_BETWEEN_U` via CMDSYS call, char   | 11 | 338 | 257 | 332 | 332.6 |
| Native unsigned compare (hypothetical), int | 9 | 275 | 196 | 269 | 269.6 |
| `IS_BYTE_BETWEEN` aligned, char        | 8  | 216 | 193 | 216 | 215.3 |
| `IS_BYTE_BETWEEN` unaligned, char      | 10 | 257 | 234 | 257 | 256.3 |
| `IS_BYTE_BETWEEN` unaligned, r=1, char | 9  | 249 | 226 | 249 | 248.3 |

The `r=1` row is a special case of the unaligned form. When the misalignment
`r` is exactly 1, the `x - r` is `x - 1`, which ccish lowers to a single `DECR`
(one byte, 33 cycles) rather than a `SUBI 1` (two bytes, 41 cycles). So the `r=1`
case is one byte smaller *and* eight cycles faster than the general unaligned
form, for the same width and mask. For example, `c1 == 49`, `c2 == 56` (width 8,
`r == 49 & 7 == 1`, base 48, mask 248) lowers to
`LLB 0; DECR; ANDI 248; CB 48; BRNE 0` (9 bytes), versus the `r=2` form's
`LLB 0; SUBI 2; ANDI 248; CB 48; BRNE 0` (10 bytes). The `DECR` borrow (39
cycles instead of 33) happens only when the byte is 0, a rare case for these
ranges, so 33 is the planning figure. This was checked directly: ccish and gcc
agree on the in/out pattern for the whole 0 to 255 sweep.

### How the average is computed

We guess `x` is usually a byte (0 to 255, so 256 possible values) and that the
range `[c1, c2]` is narrow: only 8 of the 256 values fall "in" the range. The
other 248 are split evenly, 124 below `c1` and 124 above `c2`. The `average`
is the workload-weighted mean of the three per-case cycle counts:

    average = (8 * in + 124 * (x &lt; c1) + 124 * (x &gt; c2)) / 256

For example, the aligned byte form: `(8*193 + 124*216 + 124*216) / 256 =
55112 / 256 = 215.3`. The calculation was done with a small program, not by
hand, to avoid arithmetic slips.

This is a rough estimator, not an exact expectation. It assumes a uniform
distribution over the 256 byte values, a range of width exactly 8, and no
correlation between successive queries. It is meant only to rank the forms
under a "narrow range in a byte" workload, which is the common case for
character-class and small-set tests. The per-case cycle counts depend only on
the construct, not on the specific constants, so the average applies to any
width-8 byte range. A different "in" fraction (a wider or narrower range) would
change the ranking: the forms that bail early on `x < c1` (the obvious form)
win as the below-range fraction grows, and the branchless in-range forms win as
the in-range fraction grows.

### Trade-offs

* **`IS_BYTE_BETWEEN` (aligned) is the clear winner** when it applies: smallest
  (8 bytes) and fastest on average (215.3). It needs `x` to be a byte and the
  range to be an aligned power-of-two block.
* **`IS_BYTE_BETWEEN` (unaligned)** is next (10 bytes, 256.3) and covers
  unaligned power-of-two blocks. It still beats every general form on both
  size and average time. It's even faster when the unaligned value is 1
  (9 bytes, 248.3 cycles).
* **A native unsigned compare** (not yet in the VM) would make the unsigned form
  the best *general* shape (9 bytes, 269.6), a good argument for adding one to
  the instruction set.
* **`IS_BETWEEN` versus the obvious form**, for a general (non-byte) value:
  `IS_BETWEEN` is 2 bytes smaller. On the other hand, the obvious form bails early on
  `x < c1`, which under this workload (124 of 256 values below) nearly offsets
  `IS_BETWEEN`'s cheaper in-range and above-range cases. On average they are
  within a few cycles of each other (char: 321.7 versus 314.2; int: 326.7
  versus 321.8). Prefer `IS_BETWEEN` for the 2 bytes saved; prefer the obvious
  form if the range is wide (more "in" values) or `x` is usually below `c1`,
  where its early-out pays off more.
* **`IS_BETWEEN_U` via the CMDSYS call** (11 bytes, 337.6 int / 332.6
  char) beats the signed-emulated unsigned form (368.6) on both size and
  time.
* **`IS_BETWEEN_U` (signed-emulated) is the worst** (368.6): the sign-bit flip
  (`CW`+`XOR`) costs two ops on every query. Use it only if an unsigned
  comparison is needed for correctness and the `isule` call is off the
  table (it is not, on any PLASMA target).

Bottom line: when `x` is a byte, reach for `IS_BYTE_BETWEEN` (aligned if you
can, unaligned otherwise). When `x` is a general integer, use `IS_BETWEEN`
(it is smaller and within a few cycles of the obvious form on average).
When an unsigned comparison is needed for correctness, call `isule` (the
CMDSYS form above) instead of the signed-emulated unsigned form.

## Implementing betweenness in the compiler

So far the better forms come from `#define` macros the programmer writes.
This section asks whether ccish should produce them itself, and where.

ccish rewrites patterns in two stages. The parser is out of the question: it
is a single pass that emits IR as it goes, with no view of the IR it has
already emitted and no knowledge of a value's type yet. The two real homes
are:

* **The IR pass** (irin.c) reads the IR and holds a window of it, rewriting
  patterns before lowering to machine ops. It already matches across control
  flow: it recognizes counted loops by matching shapes that straddle
  `IR_BRFALSE` and `IR_JUMP`, and it rewrites the De Morgan flip, the unsigned
  compare's sign-bit flip, and unsigned division, modulo, and shift by a
  constant. It is, in effect, a peephole at the IR level, and it is the only
  stage that knows a value's type as a type (`IR_LOCALB` versus `IR_LOCAL`).
* **The peephole** (peep.c) rewrites the machine ops after lowering. It
  already does many multi-op rewrites (`CN 1 ADD` to `INCR`, `ANDI m ANDI n`
  to `ANDI (m&n)`, `CW v CW v` to `CW v DUP`, dropping unused expressions),
  and it can already read a constant's value out of any form (`CN`, `CB`,
  `CW`, `CFFB`) and a load's width (`LLB` versus `LLW`).

### The shape, and what each stage sees

In the op stream, `if (x >= c1 && x <= c2)` is a fixed eight-op shape:

~~~~
LLx n    const c1    ISGE    BRFLS L
LLx n    const c2    ISLE    BRFLS L
~~~~

where `LLx` is `LLW` or `LLB`, the two loads are the same `n`, and both
branches go to the same `L`. The transform rewrites it to the `CW`+`ADD`+`CW`
+`ISGE` shape (general) or the `ANDI`+`BRNE` shape (byte, power-of-two block).
Both stages can see the shape: the IR pass as IR ops, the peephole as machine
ops. The difference is the type: the IR pass knows `x` is a byte because the
load is `IR_LOCALB`; the peephole can only tell because the load is `LLB`.

### Difficulty

* **General form** (any int, any constants): a fixed eight-op match with two
  equalities to check (same load, same branch target) and two constants to
  read. In the IR pass the across-branch matching is already there (the loop
  recognizer does it), so this is a new shape in an existing mechanism. In the
  peephole it is a new case in the existing linear matcher. Both are real but
  bounded work.
* **Byte form**: the general match plus (a) the value is a byte and (b) `[c1,
  c2]` is a power-of-two block. (b) is a small compile-time computation; (a)
  is a type the IR pass has directly and the peephole only by reading the
  load. This is the part that is more than a pattern match: it is a small
  data-flow fact (the value is a byte) deciding a different lowering.

### Pros and cons of building it in

Pros:

* Automatic: every `x >= c1 && x <= c2` gets the better form with no macro,
  and the programmer cannot forget it or mistype the range.
* Preconditions are checked, not trusted: the byte form is emitted only when
  the value really is a byte and the range really is a power-of-two block, so
  the macro's "treat x as its low byte" contract stops being something the
  programmer has to guarantee by hand.
* One place to get it right and test, instead of a macro to `#include` in
  every program that wants it.

Cons:

* Compiler size, the live constraint for a self-hosting 64 K target. The
  matcher, the byte test, and the power-of-two test are new code in the
  compiler that stays for every program it compiles, most of which have no
  betweenness at all.
* A larger correctness surface. A macro is checked as ordinary C, so a
  mistake is a compile error or a wrong answer in the program that uses it. A
  built-in rewrite is checked only by the compiler's own tests; a subtle miss
  (a value that is almost a byte, a range that is almost a power-of-two
  block) is a silent wrong-code bug.
* A small, local win: about two bytes for the general form, a little more for
  the byte form, per occurrence.

### Implementation location

If any of these transforms are to be done by the compiler,
the **IR pass** is probably the best place for them:

* The IR pass is the right home, not the VM code peephole: it is the IR-level
  peephole, it already matches shapes across `BRFLS` and `JUMP` (the loop
  recognizer), and it knows the type as a type. The peephole could do it too
  (it reads the load's width and the constant's form), but it would be
  inferring the type from the op rather than using it.
* Emit only the general `CW`+`ADD`+`CW`+`ISGE` shape at first. It is correct
  for any int, needs no type beyond the load's width, and is the always-safe win.
* Add the byte `ANDI` form only if the general form proves its worth. It is
  the part that needs the data-flow fact (the value is a byte) and the
  power-of-two test, and it is the part most likely to misfire silently. In
  the IR pass the type is available, so that is where the byte form belongs
  when it is wanted.

That ordering gets the automatic, always-correct win first and defers the one
that needs a data-flow fact until the machinery for it is wanted.

### What is implemented

The IR pass (irin.c's between_rewrite) implements the byte form, and only
the byte form. It matches the eight-op shape (the same char local loaded
twice, each time compared against a constant, x >= c1 (or >) and x <= c2
(or <), the two fails both branching to the same label), and fires only
when the range is a power-of-two block of bytes (low and high within 0
to 255, and the width a power of two). It emits the general unaligned
shape, one load, the misalignment r subtracted, the block's mask ANDed,
and one compare to the aligned base, and the peephole stage makes it best:
an r of 0 drops the x - 0 (the aligned form), an r of 1 becomes a DECR,
and the compare's BRFALSE meets the ISEQ as a BRNE. A general int, or a
range that is not a power-of-two block of bytes, keeps the two
comparisons: the general one-add form (IS_BETWEEN) was tried and removed
again, because it is not much smaller than the obvious form, and the
extra complexity is worth it only with a native unsigned compare, which
the VM does not have. The transform's meaning is cross-tested against
the host compiler (tests/xrun/xbetween.c), and the machine forms and
non-forms are pinned by tests/gen/genbetween.c.
