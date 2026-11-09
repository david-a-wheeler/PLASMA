// xnoop.c - the no-op drops (x | 0, x ^ 0, x << 0, x >> 0, and ~~x):
// each leaves its operand unchanged, so the cross-check runs the folded
// code and the host's unfolded code and requires the same value. The
// operand is a function of x (not x itself), so a wrong drop would show;
// w16() keeps the result in the low 16 bits, and the count-bounded
// high-x loop exercises the 16-bit wrap.
#include "ptest.h"

static int base(int x) { return x + 1000; }
int or0(int x)    { return base(x) | 0; }
int xor0(int x)   { return base(x) ^ 0; }
int shl0(int x)   { return base(x) << 0; }
int shr0(int x)   { return base(x) >> 0; }
int dblcomp(int x){ return ~~base(x); }

int main(void)
{
  int x, i;
  for (x = 0; x < 32; x++)
  {
    puti(w16(or0(x))); putc(' ');
    puti(w16(xor0(x))); putc(' ');
    puti(w16(shl0(x))); putc(' ');
    puti(w16(shr0(x))); putc(' ');
    puti(w16(dblcomp(x))); putc(' ');
  }
  for (i = 0; i < 16; i++)
  {
    x = 32752 + i;
    puti(w16(or0(x))); putc(' ');
    puti(w16(dblcomp(x))); putc(' ');
  }
  putln();
  return 0;
}
