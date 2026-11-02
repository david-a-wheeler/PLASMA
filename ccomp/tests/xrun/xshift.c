/* xshift.c — shifts and multiplies at run time, with loop constants
 * chosen to hit the peephole's multiply rewrites (x * 2 becomes
 * DUP ADD, x * 3 DUP IDXW, x * 2^n a shift) and to leave plain MULs
 * where they don't apply. */
#include "ptest.h"

int shl(int a, int b)
{
  return a << b;
}

int shr(int a, int b)
{
  return a >> b;
}

int mul(int a, int b)
{
  return a * b;
}

int main(void)
{
  int i, s;
  puti(shl(3, 4));      // 48
  putln();
  puti(shr(-16, 2));    // -4
  putln();
  puti(mul(7, 2));      // 14
  putln();
  puti(mul(7, 3));      // 21
  putln();
  puti(mul(7, 4));      // 28
  putln();
  puti(mul(7, 16));     // 112
  putln();
  puti(mul(7, 5));      // 35: no rewrite, a plain multiply
  putln();
  s = 0;
  for (i = 0; i < 8; i++)
    s += i * 2;         // 56
  puti(s);
  putln();
  s = 0;
  for (i = 1; i < 8; i += 2)
    s += i * 3;         // 48
  puti(s);
  putln();
  s = 0;
  for (i = 0; i < 5; i++)
    s += 1 << i;        // 31
  puti(s);
  putln();
  return 0;
}
