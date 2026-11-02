/* xarith.c — arithmetic: first the constant expressions the parser
 * folds at compile time (the peephole folds what it leaves), then
 * every binary operator at run time through show_ops, whose operands
 * are parameters the folder can't touch. The mixed operators are
 * unparenthesized on purpose: C's precedence is the test, and the
 * file is compiled with -Wno-parentheses (xarith.flags). */
#include "ptest.h"

int prec(int a, int b, int c, int d)
{
  return a + b * c - d;    // * binds tighter than + and -
}

int precd(int a, int b, int c, int d, int e)
{
  return a * b / c + d % e; // * / % bind tighter than +
}

/* Every binary operator, at run time. b is small, so the shifts are
 * defined, and every value stays in the 16-bit range. / and % follow
 * C (toward zero, the sign of the dividend), and >> is arithmetic. */
int show_ops(int a, int b)
{
  puti(a + b);
  putln();
  puti(a - b);
  putln();
  puti(a * b);
  putln();
  puti(a / b);
  putln();
  puti(a % b);
  putln();
  puti(a & b);
  putln();
  puti(a | b);
  putln();
  puti(a ^ b);
  putln();
  puti(a << b);
  putln();
  puti(a >> b);
  putln();
  puti(a < b);
  putln();
  puti(a <= b);
  putln();
  puti(a > b);
  putln();
  puti(a >= b);
  putln();
  puti(a == b);
  putln();
  puti(a != b);
  putln();
  return 0;
}

int main(void)
{
  puti(3 << 4);            // 48
  putln();
  puti(-5 * 3 + 7);        // -8
  putln();
  puti(~12);               // -13
  putln();
  puti(100 % 7);           // 2
  putln();
  puti(-100 / 7);          // -14
  putln();
  puti(-100 % 7);          // -2
  putln();
  puti(1 < 2 && 2 <= 2);   // 1
  putln();
  puti(3 > 4 || 5 != 5);   // 0
  putln();
  puti(!(3 == 3));         // 0
  putln();
  puti(7 & 3 | 8);         // 11: & binds tighter than |
  putln();
  puti(1 << 3 | 2);        // 10: << binds tighter than |
  putln();
  puti(2 < 3 == 1);        // 1: < binds tighter than ==
  putln();
  puti(25 ^ 3);            // 26
  putln();
  puti(20 >> 2);           // 5
  putln();
  puti(-20 >> 2);          // -5
  putln();
  puti(prec(2, 3, 4, 1));  // 13
  putln();
  puti(precd(6, 4, 3, 7, 5));  // 8+2 = 10
  putln();
  show_ops(37, 3);
  show_ops(-37, 3);
  return 0;
}
