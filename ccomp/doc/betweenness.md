# Efficiently computing betweenness

In many situations we must determine if an integer
is between two constants (inclusively). Thus, it's important
to do this efficiently for any two constants.

In C, the "obvious" way to determine if a value `x` is
between constants C1 and C2 is to compute: `(x >= C1) && (x <= C2)`
However, this translates to loading two constants, two comparisons,
and computing an "and" which involves branching. That ends up being a lot of code
on small systems. This page is about doing better.

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
costs more than the native signed comparison that the next section turns into
`IS_BETWEEN`. We could do it with a call, but a call more costly in time.
On a host compiler the unsigned `<=` is a single native instruction, but not in
our VM (PLASMA) at this time.

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

### Extending for unaligned values

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
