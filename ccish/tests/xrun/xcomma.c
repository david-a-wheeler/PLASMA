// xcomma.c - the comma operator (C11 6.5.17), cross-checked against
// the host compiler: the same sequence as tests/run/comma.c, through
// ptest.h. The two &&'s whose left side the host warns about
// (-Wunused-value, -Werror) are cast to void; that changes nothing
// observable.
#include "ptest.h"

struct pt { int x, y; };
struct pt s = {7, 8};
int a[4] = {10, 20, 30, 40};
int n;

// Add 1 to n and return its new value.
int bump(void)
{
  n = n + 1;
  return n;
}

// Print v and a space.
void show(int v)
{
  puti(v);
  putc(' ');
}

// Return a + b.
int add(int a, int b)
{
  return a + b;
}

// Return v + 1, through a comma operator in a return.
int last(int v)
{
  return v++, v;
}

int main(void)
{
  int i, j, x, sum;
  int *p;
  struct pt t;

  x = (bump(), bump(), n * 10);          // n 2
  show(x);                               // 20
  x = (n = 5, n + 1);                    // n 5
  show(x);                               // 6
  sum = 0;
  for (i = 0, j = 3; i < j; i++, j--)
    sum = sum + a[i] * a[j];             // 10 * 40 + 20 * 30
  show(sum);                             // 1000
  show(i);                               // 2
  show(j);                               // 1
  x = 1 ? (bump(), 3) : 4;               // n 6
  show(x);                               // 3
  show(n);                               // 6
  x = 2 + (n++, n);                      // n 7
  show(x);                               // 9
  p = a;
  x = *((n++, i) + p);                   // n 8; i is 2: a[2]
  show(x);                               // 30
  show(n);                               // 8
  x = (n++, s).y;                        // n 9
  show(x);                               // 8
  t = (n++, s);                          // n 10
  show(t.x);                             // 7
  p = (n++, a);                          // n 11
  show(p[3]);                            // 40
  if ((n++, n == 12))                    // n 12
    show(1);                             // 1
  else
    show(0);
  i = 0;
  while (x = a[i], x < 30)
    i++;
  show(i);                               // 2
  x = ((void)(n > 0 && bump()), 5);      // n 13
  show(x);                               // 5
  show(n);                               // 13
  n++, show(n);                          // n 14: 14
  x = (n++, 3) * 4;                      // n 15
  show(x);                               // 12
  x = (n = 1, n = n + 1, n * 5);         // n 2
  show(x);                               // 10
  x = (n++, bump)();                     // n 3, then 4
  show(x);                               // 4
  show(last(5));                         // 6
  x = add((n++, 3), 4);                  // n 5; two arguments
  show(x);                               // 7
  x = *((void)(n > 4 && bump()), i + p); // n 6; i is 2: a[2]
  show(x);                               // 30
  show(n);                               // 6
  putln();
  return 0;
}
