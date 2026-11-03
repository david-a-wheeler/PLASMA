#include "ptest.h"

/* Tail recursion (approach.md decision 33): a function's own tail
 * call - the call's result is returned directly - runs under ccish
 * and plvm with a constant frame, and under the host compiler as
 * plain recursion; the answers must agree. The non-tail functions
 * (x0, x6) are the control: a self call whose result is used stays
 * a call. Depths stay small: the host runs these without the
 * optimization. */
int x0(int n)              /* not a tail call: the result is added to */
{
  if (n <= 0)
    return 100;
  return x0(n - 1) + 1;
}
int x1(int n)              /* a tail call */
{
  if (n <= 0)
    return 100;
  return x1(n - 1);
}
int x2(int n, int acc)     /* a tail call with an accumulator */
{
  if (n <= 0)
    return acc;
  return x2(n - 1, acc + n);
}
void x3(int n, int *p)     /* a tail call, void, through a pointer */
{
  if (n <= 0)
    return;
  *p = *p + n;
  x3(n - 1, p);
}
int x4(int n)              /* a tail call in a for body */
{
  int i, s = 0;
  for (i = 0; i < n; i++)
  {
    if (i == 3)
      return x4(i);
    s += i;
  }
  return s;
}
int x5(int n, int acc)     /* a tail call in a while body */
{
  int i = 0;
  while (i < n)
  {
    if (n - i < 3)
      return x5(n - i - 1, acc + i);
    i++;
  }
  return acc;
}
int x6(int n)              /* not a tail call: the result is multiplied */
{
  if (n <= 0)
    return 1;
  return n * x6(n - 1);
}
int main(void)
{
  int p;
  puti(x0(100)); putln();
  puti(x1(2000)); putln();
  puti(x2(200, 0)); putln();
  p = 0;
  x3(100, &p);
  puti(p); putln();
  puti(x4(10)); putln();
  puti(x4(2)); putln();
  puti(x5(10, 0)); putln();
  puti(x6(5)); putln();
  return 0;
}
