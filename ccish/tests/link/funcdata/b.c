// The other half of funcdata: see a.c.
static int neg(int x) { return -x; }
int b_triple(int x) { return 3 * x; }
int (*b_neg)(int) = neg;
