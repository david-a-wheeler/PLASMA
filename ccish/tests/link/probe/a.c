// Shared names whose starting slots collide (approach.md issue 11,
// "Fitting the linker's tables"). With 307 slots, f108018, f108042 and
// f108050 all start at slot 306, the last, so the second and third wrap
// around to slots 0 and 1. In b.c, f108019 starts at slot 0 and f108028
// at slot 1, so both are pushed past the slots taken, and b.c finds
// this object's names past their starting slot. (A new MAX_GSYMS needs
// new names here.)
// listing-has: ; shared names: 9 of 307 slots, 79 of 3000 name bytes
int f108042 = 10;

// One more than n.
int f108018(int n) { return n + 1; }

// Twice n.
int f108050(int n) { return n * 2; }
