// xzerodup - x * 0 is always 0 (the DROP CN 0 folds to one ANDI 0), and
// adding two equal word values is unchanged when the CW constant is pushed
// once and duplicated instead of twice. The values are the same either way;
// the cross-test is the oracle for both folds.
#include "ptest.h"
int zero(int x){ return x * 0; }
int dargs(int a, int b){ return a + b; }
int main(void)
{
  int x;
  for (x = 0; x < 16; x++)
  {
    puti(w16(zero(x))); putc(' ');
  }
  puti(w16(dargs(1000, 1000))); putc(' ');
  puti(w16(dargs(10000, 10000))); putc(' ');
  putln();
  return 0;
}
