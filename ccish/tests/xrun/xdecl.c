// xdecl.c — the declaration and storage keywords: typedef (of a
// scalar and of an anonymous struct), enum with a skipped value,
// the signed and unsigned specifiers, const (a compile-time-known
// scalar), volatile, static locals and globals, and an extern
// declaration before its definition.
#include "ptest.h"

typedef int count;
typedef struct {
  int x;
  int y;
} pt2;

enum color { RED = 1, GREEN, BLUE = 20 };

static int scount;

extern int efn(int);

int efn(int x)
{
  return x * 2;
}

int counter(void)
{
  static int n;
  n += 1;
  return n;
}

int main(void)
{
  volatile int vx = 3;
  const int c = 5;
  signed int sv = -15;
  unsigned int uv = 40;
  count k = 12;
  pt2 p;
  vx += 4;
  k += 3;
  p.x = 3;
  p.y = 5;
  scount = 5;
  scount += counter();
  puti(vx);               // 7
  putln();
  puti(c * 3 + 2);        // 17: a const scalar is compile-time known
  putln();
  puti(k);                // 15
  putln();
  puti(p.x + p.y);        // 8
  putln();
  puti(scount);           // 6: a static global, plus a static local
  putln();
  puti(counter());        // 2
  putln();
  puti(sv + uv);          // 25: signed plus unsigned, small values
  putln();
  puti(efn(7));           // 14: declared extern before defined
  putln();
  puti(GREEN + BLUE);     // 22: enum, with a skipped value
  putln();
  return 0;
}
