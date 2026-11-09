// xchar.c — char: unsigned in ccish by design, and in the host build
// via -funsigned-char; the arithmetic must agree, including storing a
// wider value in a char (the low byte is kept).
#include "ptest.h"

int main(void)
{
  char c, b;
  int i, s;
  c = 200;
  puti(c);               // 200: the char is unsigned
  putln();
  c = c + 100;           // 300 kept as its low byte: 44
  puti(c);
  putln();
  puti((int)c * 2);      // 88
  putln();
  b = 'A';
  puti(b + 1);           // 66
  putln();
  puti(b < 'Z');         // 1
  putln();
  s = 0;
  for (i = 1; i < 10; i++)
    s += (int)(char)i;
  puti(s);               // 45
  putln();
  putc('x');
  putc(32);
  putc('y');
  putln();
  return 0;
}
