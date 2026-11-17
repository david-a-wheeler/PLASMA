// xbooltype.c: C23's bool (ccish's TY_BOOL, src/type.c): a 1-byte type
// that holds only 0 or 1. Any nonzero value assigned to a bool is
// normalized to 1, in every path that gives a bool its value:
// assignment (plain, compound, ++ and --), initialization (local,
// global, array and struct element), call arguments, and return. The
// spellings are bool and _Bool, and true and false are 1 and 0.
#include <stdbool.h>
#include "ptest.h"

struct S {
  bool b;
  int x;
};

// A bool argument and return value: both normalized.
static bool bfun(int v)
{
  return v;
}

static int cfun(bool b)
{
  return b * 10;
}

static bool bsum(bool a, bool b)
{
  return a + b;
}

bool g = 2;
bool gn = -3;
const bool cg = 5;
bool ga[3] = { 1, 0, 7 };
struct S gs = { 9, 42 };

int main(void)
{
  bool a = 5, b = 0, c = true, d = false;
  _Bool e = 3;
  bool arr[3] = { 1, 0, 2 };
  struct S s = { 7, 42 };
  bool *p = &a;
  int i, r = 0;

  // Assignments normalize (5, 0, 1, 0: a's new value, b, c, d):
  a = 9;
  a += 0;
  b = !b;
  *p = 11;
  p = &c;
  *p = 0;
  r = a + b + c + d + e;
  puti(r);
  putc(' ');

  // Globals and their initializers: 2, -3 and 5 all became 1.
  puti(g + gn + cg);
  putc(' ');

  // Array and struct initializers and members, normalized:
  arr[1] = 8;
  arr[2] = 0;
  s.b = 100;
  gs.b = -1;
  puti(arr[0] + arr[1] + arr[2]);
  putc(' ');
  puti(s.b + s.x);
  putc(' ');
  puti(gs.b + gs.x);
  putc(' ');

  // Arguments and results of calls:
  puti(bfun(0) + bfun(7) + cfun(1) + cfun(0));
  putc(' ');
  puti(bsum(1, 2) + bsum(0, 0));
  putc(' ');

  // Increment and decrement of a bool: ++b is always 1; --b is 0 for
  // 1, and 1 for 0 (-1 normalizes to 1, as any nonzero value does).
  b = 0;
  ++b;
  puti(b);
  putc(' ');
  b = 1;
  --b;
  puti(b);
  putc(' ');
  b = 0;
  --b;
  puti(b);
  putc(' ');
  b = 1;
  puti(b--);
  putc(' ');
  puti(b);
  putc(' ');

  // A bool used as an int promotes to 0 or 1:
  a = 5;
  b = a;
  puti(b + 10);
  putc(' ');
  puti(a - b);
  putc(' ');

  // Conditions: a bool's value is 0 or 1, as any scalar's truth is.
  if (g)
    puti(1);
  i = 0;
  while (e) {
    e = 0;
    i = 9;
  }
  puti(i + (a && b) + (a || d) + !d);
  putc(' ');

  // sizeof: bool is 1 byte, as char is (the contract's portable form).
  puti(sizeof(bool) == sizeof(char));
  putc(' ');
  puti(sizeof arr / sizeof arr[0] == 3);
  putln();
  return 0;
}
