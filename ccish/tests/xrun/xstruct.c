/* xstruct.c — structs: member access, pointers to structs, structs
 * passed by value, structs in arrays. No sizeof of a struct (ccish
 * packs with no padding, the host may pad); the element-count ratio
 * is portable. */
#include "ptest.h"

struct pt
{
  int x;
  int y;
};

struct cell
{
  int v;
  char tag;
};

int dot(struct pt a, struct pt b)
{
  return a.x * b.x + a.y * b.y;
}

int cellsum(struct cell *c, int n)
{
  int i, s;
  s = 0;
  for (i = 0; i < n; i++)
    s += c[i].v;
  return s;
}

int main(void)
{
  struct pt p, q;
  struct pt *pp;
  struct cell c[3];
  int i, n;
  p.x = 3;
  p.y = 4;
  q.x = 5;
  q.y = 6;
  puti(dot(p, q));                  // 15+24 = 39
  putln();
  pp = &p;
  puti(pp->x);                      // 3
  putln();
  n = (int)(sizeof c / sizeof c[0]);
  puti(n);                          // 3 on both
  putln();
  for (i = 0; i < n; i++) {
    c[i].v = i * 10;
    c[i].tag = (char)(65 + i);
  }
  puti(cellsum(c, n));              // 0+10+20 = 30
  putln();
  puti((int)(c[1].tag - c[0].tag)); // 1
  putln();
  p.x = p.x + p.y;
  puti(p.x);                        // 7
  putln();
  return 0;
}
