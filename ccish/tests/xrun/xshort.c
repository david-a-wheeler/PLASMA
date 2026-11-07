/* xshort.c: `short` is a synonym for `int`. The preprocessor gives
 * the word `short` the token of `int` (ccish has one word type, and
 * the standard C distinction between the two is not worth
 * implementing), so every use below is of the ordinary int type. Each
 * line exercises one spelling: a short global, a short local, short
 * parameters and return, a short *, a short array, a short struct
 * member, a short in a for loop, and a short in a macro and a cast.
 * The two compilers' output must be identical. */
#include "ptest.h"

#define SQ(x) ((x) * (x))

short g = 1000;
short *gp;

struct P
{
  short a;
  short b;
};

struct P sp;

short f(short a, short b)
{
  return a + b;
}

short bump(short *p)
{
  *p = *p + 1;
  return *p;
}

int main(void)
{
  short s = 10;
  short arr[4];
  short *p;
  int i, t;

  /* A short global, through itself and a short *. */
  gp = &g;
  puti(f(g, *gp));
  putln();

  /* A short local and a short array. */
  for (i = 0; i < 4; i++)
    arr[i] = s + i;
  t = arr[0] + arr[1] + arr[2] + arr[3];
  puti(t);
  putln();

  /* A short * parameter: written and read through the pointer. */
  p = &s;
  t = bump(p) + *p;
  puti(t);
  putln();

  /* Short struct members. */
  sp.a = 7;
  sp.b = f(sp.a, g);
  puti(sp.a + sp.b);
  putln();

  /* A short in a for loop's count, and in a macro and a cast. */
  t = 0;
  for (s = 1; s <= 4; s++)
    t += s;
  puti(t + SQ(s) + (short)(g + 100));
  putln();

  /* The element count of a short array, the portable ratio. */
  puti(sizeof(arr) / sizeof(arr[0]));
  putln();

  return 0;
}
