/* xbool.c - truth runs (approach.md, decision 31): an if whose arms
 * return 1 and 0 is the condition's truth, so the compiler returns
 * the condition's value and drops the branch, labels and arms. The
 * cross-check runs this against the host compiler: a plain value is
 * boolified, the arms' order negates, equal arms keep the constant,
 * non-truth arms keep the branch, the ternary spelling flows the
 * value on to the use, and short-circuit conditions keep their
 * branches. */
#include "ptest.h"

int b1(int x) { if (x) return 1; else return 0; }
int b2(int x) { if (x) return 0; else return 1; }
int b3(int x) { if (x) return 1; return 0; }
int b4(int x) { if (x) return 1; return 2; }
int b5(int x) { if (x) return 7; else return 7; }
int b6(int x) { if (!x) return 1; else return 0; }
int b7(int a, int b) { if (!(a > b)) return 1; else return 0; }
int t1(int x) { return x ? 1 : 0; }
int t2(int x) { return x ? 0 : 1; }
int t3(int x) { return x ? 5 : 5; }
int t4(int x) { return x ? 1 : 2; }
int ad1(int x, int y) { y += x ? 1 : 0; return y; }
int ad2(int x, int y) { y += x ? 0 : 1; return y; }
int an1(int a, int b) { return a && b; }
int ao1(int a, int b) { return a || b; }

int main(void)
{
  int x;
  for (x = -3; x < 6; x++)
  {
    puti(w16(b1(x))); putc(' ');
    puti(w16(b2(x))); putc(' ');
    puti(w16(b3(x))); putc(' ');
    puti(w16(b4(x))); putc(' ');
    puti(w16(b5(x))); putc(' ');
    puti(w16(b6(x))); putc(' ');
    puti(w16(b7(x, 2))); putc(' ');
    puti(w16(t1(x))); putc(' ');
    puti(w16(t2(x))); putc(' ');
    puti(w16(t3(x))); putc(' ');
    puti(w16(t4(x))); putc(' ');
    puti(w16(ad1(x, 100))); putc(' ');
    puti(w16(ad2(x, 100))); putc(' ');
    puti(w16(an1(x, 3))); putc(' ');
    puti(w16(ao1(x, 0))); putc(' ');
    putln();
  }
  return 0;
}
