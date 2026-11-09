// The other half of funcaddr: see a.c.
static int neg(int x) { return -x; }
int b_double(int x) { return 2 * x; }
int b_apply(int (*f)(int), int x) { return f(x); }
int (*b_pick(int k))(int) { return k ? neg : b_double; }
