// xunion.c: unions. A union's members all start at byte 0, and the
// union is the size of its largest member, so a value written through
// one member is visible through the others. Both compilers are
// little-endian, every value stays in the low 16 bits, and nothing
// prints a layout, so every line is the same in both. Each line
// exercises one point: type punning through a char, overwriting, two
// same-size members, a union in a struct, a global, a *, an array,
// and pass by value. The two compilers' output must be identical.
#include "ptest.h"

union W
{
  int a;
  char b;
};

union P
{
  int x;
  int y;
};

struct Box
{
  char t;
  union W u;
  int n;
};

union W gw;
union W arr[3];
struct Box box;
union W *pw;

int f(union W u)
{
  return u.a + u.b;
}

int main(void)
{
  union W u;
  union P p;
  int i, t;

  // Type punning: an int's low byte is the char member.
  u.a = 0x1234;
  puti(u.b);
  putln();

  // A char write lands in the int's low byte.
  u.b = 99;
  puti(u.a);
  putln();

  // Two same-size members share the same two bytes.
  p.x = 7;
  puti(p.y);
  putln();
  p.y = 11;
  puti(p.x);
  putln();

  // A union in a struct: its values, not its layout.
  box.t = 5;
  box.u.a = 0x0040;
  box.n = 6;
  puti(box.t + box.u.b + box.n);
  putln();
  box.u.b = 8;
  puti(box.u.a);
  putln();

  // A global union, through itself and a *.
  gw.a = 100;
  pw = &gw;
  puti(pw->a + gw.b);
  putln();

  // An array of unions, through its elements.
  for (i = 0; i < 3; i++)
    arr[i].a = 10 * (i + 1);
  t = arr[0].b + arr[1].b + arr[2].b;
  puti(t);
  putln();

  // Pass by value: the union's two bytes are copied.
  u.a = 0x0102;
  puti(f(u));
  putln();

  // The element count of an array of unions, the portable ratio.
  puti(sizeof(arr) / sizeof(arr[0]));
  putln();

  return 0;
}
