// xfunc.c — functions: pass-by-value arguments, returns, nested
// calls, recursion, and a dead store the peephole drops at the
// return (nothing reads x back).
#include "ptest.h"

int add(int a, int b)
{
  return a + b;
}

int sub(int a, int b)
{
  int t;
  t = a - b;
  return t;
}

int fib(int n)
{
  if (n < 2)
    return n;
  return fib(n - 1) + fib(n - 2);
}

// The store in pick() is intentionally dead (the peephole drops it
// at the return), and assign_test() uses an assignment as a
// condition: the host compilers warn about both, so this file is
// compiled with the suppressions in xfunc.flags; ccish compiles the
// same source without them.
int pick(int n)
{
  int x;
  x = 99;                 // dead: nothing reads x back
  if (n < 0)
    return -n;
  return n * 2;
}

int assign_test(int n)
{
  int x;
  x = 0;
  if (x = n + 1)          // the assignment is the condition
    return x * 2;
  return -1;
}

int main(void)
{
  puti(add(2, 3));        // 5
  putln();
  puti(add(add(1, 2), 3)); // 6
  putln();
  puti(sub(10, 4));       // 6
  putln();
  puti(fib(10));          // 55
  putln();
  puti(pick(-7));         // 7
  putln();
  puti(pick(6));          // 12
  putln();
  puti(assign_test(2));   // x=3, so 6
  putln();
  puti(assign_test(-1));  // x=0, so -1
  putln();
  return 0;
}
