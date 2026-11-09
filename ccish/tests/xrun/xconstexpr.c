// xconstexpr.c - C23's constexpr, as named constants: usable in a
// _Static_assert, an array size, an enum value, a case label and a
// global initializer, with its type (an unsigned compares unsigned),
// and block scope (an inner one hides an outer). One name per
// constexpr, as C23 requires.
#include "ptest.h"

constexpr int N = 10;
constexpr int M = N * 2;
constexpr unsigned BIG = 0x8000;
constexpr char C = 'A';
_Static_assert(M == 20, "M is twice N");
_Static_assert((BIG & 0x7FFF) == 0, "BIG is one high bit");
enum { E = N + 1 };
int a[N];
int g = M + 1;

int pick(int x)
{
  switch (x) {
  case N - 9:
    return 100;
  case M:
    return 200;
  }
  return 0;
}

int main(void)
{
  constexpr int N = 3;          // hides the file-scope N
  int one = 1;
  _Static_assert(N == 3, "the inner N");
  puti(N); putc(' ');
  puti(sizeof(a) / sizeof(a[0])); putc(' ');
  puti(E); putc(' ');
  puti(g); putc(' ');
  // BIG is unsigned: one * BIG / 2 is 16384, where an int's -32768
  // would give -16384 in 16 bits.
  puti(one * BIG / 2 > 100); putc(' ');
  putc(C); putc(' ');
  puti(pick(1)); putc(' ');
  puti(pick(20)); putc(' ');
  puti(pick(3));
  putln();
  return 0;
}
