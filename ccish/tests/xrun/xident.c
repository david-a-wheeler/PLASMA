// xident.c - the peephole stage's algebraic identities (peep.c), cross-
// checked against the host compiler: each function is one identity,
// and each line prints its results for x over the edges of the 16-bit
// range and a sweep between them.
#include "ptest.h"

int notnot(int x)    { return !!x; }

// The values of x: the edges of the 16-bit range, and around 0.
int xs[] = { -32768, -32767, -256, -255, -3, -2, -1, 0, 1, 2, 3, 255,
             256, 32766, 32767 };

int main(void)
{
  int i, n = sizeof xs / sizeof xs[0];
  for (i = 0; i < n; i++) {
    puti(w16(notnot(xs[i]))); putc(' ');
    putln();
  }
  return 0;
}
