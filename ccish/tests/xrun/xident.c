// xident.c - the peephole stage's algebraic identities (peep.c), cross-
// checked against the host compiler: each function is one identity,
// and each line prints its results for x over the edges of the 16-bit
// range and a sweep between them.
#include "ptest.h"

int notnot(int x)    { return !!x; }
int ult0(unsigned x) { return x < 0u; }
int uge0(unsigned x) { return x >= 0u; }
int mulm1(int x)     { return x * -1; }
int xorm1(int x)     { return x ^ -1; }
int andm1(int x)     { return x & -1; }
int modm1(int x)     { return x % -1; }
int orm1(int x)      { return x | -1; }
int subself(int x)   { return x - x; }
int xorself(int x)   { return x ^ x; }
int andself(int x)   { return x & x; }
int orself(int x)    { return x | x; }
int zsub(int x)      { return 0 - x; }
// x / -1, but not for -32768, whose quotient overflows (undefined).
int divm1(int x)     { return x == -32768 ? 0 : x / -1; }

// The values of x: the edges of the 16-bit range, and around 0.
int xs[] = { -32768, -32767, -256, -255, -3, -2, -1, 0, 1, 2, 3, 255,
             256, 32766, 32767 };

int main(void)
{
  int i, n = sizeof xs / sizeof xs[0];
  for (i = 0; i < n; i++) {
    puti(w16(notnot(xs[i]))); putc(' ');
    puti(w16(ult0((unsigned)xs[i]))); putc(' ');
    puti(w16(uge0((unsigned)xs[i]))); putc(' ');
    puti(w16(mulm1(xs[i]))); putc(' ');
    puti(w16(divm1(xs[i]))); putc(' ');
    puti(w16(xorm1(xs[i]))); putc(' ');
    puti(w16(andm1(xs[i]))); putc(' ');
    puti(w16(modm1(xs[i]))); putc(' ');
    puti(w16(orm1(xs[i]))); putc(' ');
    puti(w16(subself(xs[i]))); putc(' ');
    puti(w16(xorself(xs[i]))); putc(' ');
    puti(w16(andself(xs[i]))); putc(' ');
    puti(w16(orself(xs[i]))); putc(' ');
    puti(w16(zsub(xs[i]))); putc(' ');
    putln();
  }
  return 0;
}
