/* Shared names whose starting slots collide (approach.md issue 11,
 * "Fitting the linker's tables"). With 509 slots, f108802, f108810 and
 * f108826 all start at slot 508, the last, so the second and third wrap
 * around to slots 0 and 1. In b.c, f108803 starts at slot 0 and f108804
 * at slot 1, so both are pushed past the slots taken, and b.c finds
 * this object's names past their starting slot. (A new MAX_GSYMS needs
 * new names here.) */
// listing-has: ; shared names: 9 of 509 slots, 61 of 6144 name bytes
int f108810 = 10;

/* One more than n. */
int f108802(int n) { return n + 1; }

/* Twice n. */
int f108826(int n) { return n * 2; }
