// xhoist.c - hoisting (irloop.c, hoist()) from loops that store through
// computed addresses, whose IR holds IR_SWAPs: each costly invariant
// expression is computed once, before its loop, and its place in the
// body keeps its length (a load, then padding the peephole removes), so
// the swaps' block lengths stay right. Each shape of store, with the
// expression in the value or in the address; char and int elements;
// a struct's member; a store through an element (a[c[i]]); nested
// register loops; and a run-time exchange (*q /= g(i)) beside a
// hoisted value. Both compilers must print the same.
#include "ptest.h"

struct cell { char tag; int v; };

char a[16];
int b[16];
int c[8] = {3, 1, 4, 1, 5, 0, 2, 6};
int m[4][6];
struct cell cells[8];
int d[8] = {10, 20, 30, 40, 50, 60, 70, 80};

// The sum of n chars at p.
int sumc(char *p, int n)
{
  int i, s = 0;
  for (i = 0; i < n; i++)
    s += p[i];
  return s;
}

// The sum of n ints at p.
int sumi(int *p, int n)
{
  int i, s = 0;
  for (i = 0; i < n; i++)
    s += p[i];
  return s;
}

// One more than v: a call, which keeps x op= e's order (IR_SWAP's -1).
int g(int v)
{
  return v + 1;
}

int main(void)
{
  int i, x = 4, y = 6, w = 7;
  register int j, k, rw = w;
  char *q;
  int *p;

  for (i = 0; i < 10; i++)            // in the value: a[i + 1]
    a[i + 1] = x * y / 3 + i;
  puti(sumc(a, 16)); putln();

  q = a;                              // *q++
  for (i = 0; i < 12; i++)
    *q++ = x * y / 7 + i;
  puti(sumc(a, 16)); putln();

  for (i = 0; i < 10; i++)            // int elements
    b[i + 1] = x * y / 3 - i;
  puti(sumi(b, 16)); putln();

  for (i = 0; i < 8; i++)             // in the address too
    b[i + x * y / 12] = (x + 1) * y;
  puti(sumi(b, 16)); putln();

  for (i = 0; i < 8; i++) {           // a struct's members
    cells[i].tag = x * 5 / y + i;
    cells[i].v = x * w / 5 - i;
  }
  k = 0;
  for (i = 0; i < 8; i++)
    k += cells[i].tag * 3 + cells[i].v;
  puti(k); putln();

  for (i = 0; i < 8; i++)             // through an element
    a[c[i]] = x * y / 5 + i;
  puti(sumc(a, 16)); putln();

  for (j = 0; j < 4; j++)             // nested, register: j * rw / 3
    for (k = 0; k < 5; k++)           // is the inner loop's invariant
      m[j][k + 1] = j * rw / 3 + k;
  p = &m[0][0];
  puti(sumi(p, 24)); putln();

  k = 0;                              // a run-time exchange, and a
  for (i = 0; i < 5; i++) {           // hoisted x * y / 3
    p = &d[i];
    *p /= g(i);
    k += x * y / 3 + d[i];
  }
  puti(k); putln();
  return 0;
}
