// xstructcopy.c: copying a whole struct when the destination is an
// element reached through a pointer, so no destination word is a
// variable at a fixed offset and each one needs its address computed.
// The source is a struct returned by value, its words coming off the
// stack one by one, from one word to four, and another such element,
// the small one word by word and the big one through the copy helper.
// The values pin each copied word.
#include "ptest.h"

struct tw
{
  int v;
};

struct four
{
  int a;
  int b;
};

struct wide
{
  int a, b, c, d;  // 8 bytes: four words, the most a return has
};

struct four mk(int k)
{
  struct four s;
  s.a = k;
  s.b = k * 2;
  return s;
}

struct wide mkw(int k)
{
  struct wide w;
  w.a = k;
  w.b = k + 1;
  w.c = k + 2;
  w.d = k + 3;
  return w;
}

int main(void)
{
  struct four f[3], g[3];
  struct tw t[3];
  struct wide w[2], x[2];
  struct four *pf = f, *pg = g;
  struct tw *pt = t;
  struct wide *pw = w, *px = x;
  int i, s;

  for (i = 0; i < 3; i++) {
    f[i].a = i;
    f[i].b = i + 10;
    t[i].v = i;
  }
  for (i = 0; i < 2; i++) {
    w[i].a = i;
    w[i].d = i + 20;
  }

  // A returned struct into an element reached through a pointer: one
  // word, two, and four.
  pt[2].v = 1;
  pf[1] = mk(3);
  puti(pf[1].a);          // 3
  putln();
  puti(pf[1].b);          // 6
  putln();
  px[0] = mkw(5);
  s = px[0].a;
  puti(s);                // 5
  putln();
  puti(px[0].d);          // 8
  putln();

  // Element to element: the small one word by word.
  pt[0] = pt[2];
  puti(pt[0].v);          // 1
  putln();
  pt[1] = pt[0];
  puti(pt[1].v);          // 1
  putln();

  // And the big one through the copy helper.
  pg[2] = pf[1];
  puti(pg[2].a + pg[2].b);  // 9
  putln();
  px[1] = pw[0];
  s = px[1].a;
  puti(s);                // 0
  putln();
  puti(px[1].d);          // 20
  putln();

  // The untouched elements are untouched.
  puti(f[0].a + f[2].b);  // 0 + 12
  putln();
  puti(w[1].a + w[1].d);  // 1 + 21
  putln();
  return 0;
}
