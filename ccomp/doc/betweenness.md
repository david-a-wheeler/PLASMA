# Efficiently computing betweenness

In many situations we must determine if an integer
is between two constants (inclusively). Thus, it's important
to do this efficiently for any two constants.

In C, the "obvious" way to determine if a value `x` is
between constants C1 and C2 is to compute: `(x >= C1) && (x <= C2)`
However, this translates to loading two constants, two comparisons,
and computing an "and" which involves branching. That ends up being a lot of code
on small systems. Can we do better?

"Hacker's Delight" pages 51-52 provides a good solution if the system has an
efficient unsigned comparison operator. It exploits the fact that you can subtract the
first constant, causing numbers less than zero to "wrap around" and be interpreted as
huge unsigned integers. However, in our VM, only signed integer comparisons are built-in.

Good news, we *can* do this comparison efficiently using only signed int comparisons!

## Approach

We'll presume we're using 2's complement signed integer arithmetic and C1 <= C2.

We'll efficiently compute BETWEEN(x, low, high) == `(x >= C1) && (x <= C2)`.
Instead subtracting by C1 to force wrapping around 0 of values we don't want,
we'll add enough to force wrapping of unwanted nubmers
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

## C code

The proposed C code is:

~~~~
const int MY_MAX_INT = (int) (~0U >> 1);
#define IS_BETWEEN(x, c1, c2) ((int) (x) + (MY_MAX_INT - c2) >= (MY_MAX_INT - c2 + c1))
~~~~

Note that `MY_MAX_INT` could also be a #define; it's shown as a constant here so
that the copmiler front end doesn't need to recompute and re-fold the constant every time
`IS_BETWEEN` is used.

## Specialized optimizations

In some cases this can be optimized further using `AND`, but this depends
on the type of `x` and the values of `c1` and `c2`.
If `c1` ends with some 0 bits, and `c2` is `c1` with bottom zero bits filled,
we can use `AND`.

I haven't worked out what that transformation would be. Here are some notes.

* ANDI (byte x) : ANDI + CN/CB + ISEQ = 4–5
* full-word mask (word x) : CW · AND + CN/CB/CW + ISEQ = 6–8

If the compiler front-end finds the pattern `x + CONSTANT1 >= CONSTANT2` for integers, or
the compiler back-end uses a stack and sees
the pattern `CONSTANT1`, `+`, `CONSTANT2`, `signed >=`,
then the compiler might be able to simplify that further into "AND with a constant"
in cases where that would be more efficient.
Then we don't need to tweak the `IN_BETWEEN` for that special case.

In our case, we generate an IR that differentiates between `signed >=` (IRGE) and `unsigned >=`,
so that is a possibility.
The first constant would be the value of `MY_MAX_INT - c2` and the second would be
the value of `MY_MAX_INT - c2 + c1`.
