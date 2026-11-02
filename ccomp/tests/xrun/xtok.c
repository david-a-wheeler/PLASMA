/* xtok.c — token coverage: the literal forms (hex, octal, the u
 * suffix, and hex over $7FFF in its unsigned form), the character
 * escapes, adjacent string concatenation, and the expression forms
 * the other tests leave out: the ternary ?: and the switch
 * statement. The comma operator and the ... token are lexed but the
 * parser doesn't support them yet (errors 24 and 150), so they have
 * no cross tests. */
#include "ptest.h"

int max3(int x, int y, int z)
{
  return x > y ? (x > z ? x : z) : (y > z ? y : z);
}

int sw(int n)
{
  int r;
  r = 0;
  switch (n) {
  case 0:
    r = 100;
    break;
  case 1:
    r = 200;              // falls through
  case 2:
    r += 5;
    break;
  case 3:
    r = 300;
    break;
  default:
    r = n * 10;
  }
  return r;
}

int main(void)
{
  puti(0x1F + 010 + 5u);  // 31+8+5 = 44
  putln();
  puti(0x8000 > 0x7FFF);  // 1: an unsigned hex over $7FFF, compared
  putln();
  putc('\x41');
  putc('\101');
  putc('\\');
  putc('\'');
  putln();                // A, A, a backslash, a quote
  puts(pstr("ab" "cd"));
  putln();                // "abcd"
  puti(max3(15, 25, 7));  // 25
  putln();
  puti(max3(7, 3, 9));    // 9
  putln();
  puti(max3(-2, 5, 5));   // 5
  putln();
  puti(sw(0));            // 100
  putln();
  puti(sw(1));            // 205: case 1 falls through
  putln();
  puti(sw(2));            // 5: case 2 hit directly
  putln();
  puti(sw(3));            // 300
  putln();
  puti(sw(7));            // 70: default
  putln();
  return 0;
}
