// xcondptr.c: a conditional expression with pointer operands, where
// the type of the result is decided (pexpr.c, cond_type): a null
// pointer constant on either side, giving the other side's type (and
// 0 on the machine), and a void * meeting a const void *, giving a
// void * with the qualifiers of both. The values pin which pointer
// each result held.
#include "ptest.h"

int pick(int c, int *p)
{
  int *q;
  q = c ? p : 0;
  return q ? *q : -1;
}

int pickn(int c, int *p)
{
  int *q;
  q = c ? 0 : p;
  return q ? *q : -1;
}

int pickv(int c, int *x, int *y)
{
  void *vx = x;
  const void *vy = y;
  const void *r;
  r = c ? vx : vy;
  return *(int *)r;
}

int main(void)
{
  int a = 11, b = 22;
  puti(pick(1, &a));     // *a
  putln();
  puti(pick(0, &a));     // null: -1
  putln();
  puti(pickn(0, &b));    // *b
  putln();
  puti(pickn(1, &b));    // null: -1
  putln();
  puti(pickv(1, &a, &b));  // *a
  putln();
  puti(pickv(0, &a, &b));  // *b
  putln();
  return 0;
}
