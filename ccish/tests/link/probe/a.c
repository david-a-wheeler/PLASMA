/* Shared names whose starting slots collide (approach.md issue 11,
 * "Fitting the linker's tables"). With 163 slots, f108188, f108284 and
 * f108296 all start at slot 162, the last, so the second and third wrap
 * around to slots 0 and 1. In b.c, f108189 starts at slot 0 and f108198
 * at slot 1, so both are pushed past the slots taken, and b.c finds
 * this object's names past their starting slot. (A new MAX_GSYMS needs
 * new names here.) */
// listing-has: ; shared names: 9 of 163 slots, 61 of 1427 name bytes
int f108284 = 10;

/* One more than n. */
int f108188(int n) { return n + 1; }

/* Twice n. */
int f108296(int n) { return n * 2; }
