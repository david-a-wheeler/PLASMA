/* Shared names whose starting slots collide (approach.md issue 11,
 * "Fitting the linker's tables"). With 149 slots, f100198, f100286 and
 * f100294 all start at slot 148, the last, so the second and third wrap
 * around to slots 0 and 1. In b.c, f100199 starts at slot 0 and f100000
 * at slot 1, so both are pushed past the slots taken, and b.c finds
 * this object's names past their starting slot. (A new MAX_GSYMS needs
 * new names here.) */
// listing-has: ; shared names: 9 of 149 slots, 61 of 1281 name bytes
int f100286 = 10;

/* One more than n. */
int f100198(int n) { return n + 1; }

/* Twice n. */
int f100294(int n) { return n * 2; }
