/* xaddsub.c - the additive-update combine (two small constant adds or
 * subtracts merge to one op): the cross-check runs the folded code and
 * the host's unfolded code and requires the same value. The constants
 * are small, so the low 16 bits agree under either width; w16() keeps
 * the printed result there. The high-x loop is count-bounded (not
 * x-bounded) so it stops on the 16-bit VM, and exercises the 16-bit
 * wrap of the intermediate. */
#include "ptest.h"

int sum(int x)    { return x + 5 - 3; }
int zero(int x)   { return x + 5 - 5; }
int cross(int x)  { return x + 1 - 3; }
int triple(int x) { return x + 7 - 2 + 3; }

int main(void)
{
  int x, i;
  for (x = 0; x < 32; x++)
  {
    puti(w16(sum(x))); putc(' ');
    puti(w16(zero(x))); putc(' ');
    puti(w16(cross(x))); putc(' ');
    puti(w16(triple(x))); putc(' ');
  }
  for (i = 0; i < 16; i++)
  {
    x = 32752 + i;
    puti(w16(sum(x))); putc(' ');
    puti(w16(triple(x))); putc(' ');
  }
  putln();
  return 0;
}
