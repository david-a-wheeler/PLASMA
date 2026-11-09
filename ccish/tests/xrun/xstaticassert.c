// xstaticassert.c - _Static_assert, which the host oracle checks too:
// at file scope, in a block and between a block's declarations, over
// arithmetic, a macro, an enum constant, a cast, a conditional and a
// character constant. No sizeof: its values differ on the host.
#include "ptest.h"

#define TEN 10
enum { E_ONE = 1, E_TWO };

_Static_assert(1 + 1 == 2, "arithmetic");
_Static_assert(TEN * 2 == 20, "a macro");
_Static_assert(E_TWO == E_ONE + 1, "enum constants");
_Static_assert((int)'a' - 'A' == 32, "a cast and character constants");
_Static_assert((1 ? 2 : 3) == 2, "a conditional");
_Static_assert((TEN & 6) == 2 && (TEN | 5) == 15, "bit operations");

int f(int x)
{
  int y = x + 1;
  _Static_assert(1, "between declarations");
  int z = y * 2;
  return z;
}

int main(void)
{
  _Static_assert(E_ONE, "in a block");
  puti(f(20)); putln();
  return 0;
}
