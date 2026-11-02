/* xarith.c — arithmetic: first the constant expressions the parser
 * folds at compile time (the peephole folds what it leaves), then
 * every binary operator at run time through show_ops, the compound
 * assignments one by one, the same operators in multi-level
 * expressions, whose partial results must sit on the arithmetic
 * stack while the next tier is computed: at run time (the operands
 * are parameters the folder can't touch), left- and right-nested,
 * and folded as deep constants. The mixed operators are
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

/* Two tiers: * / % over + -, six operands. */
int two_tiers(int a, int b, int c, int d, int e, int f)
{
  return a + b * c - d / e % f;
}

/* Four tiers: * / % over + -, then ^, then |. */
int four_tiers(int a, int b, int c, int d, int e, int f, int g, int h)
{
  return a + b * c - d / e % f ^ g | h;
}

/* Right-nested: unlike the left-associative chains above, each level
 * leaves its partial result on the stack while the next is computed,
 * so the stack grows one word per level. */
int right_nest(int a, int b, int c, int d, int e, int f)
{
  return a + (b + (c + (d + (e + f))));
}

/* Subexpressions under operators: the division's divisor is a
 * subexpression, so it must be computed and held while the
 * numerator's is, and the product's factor is held for the whole
 * division. */
int mixed(int a, int b, int c, int d, int e, int f, int g)
{
  return a * (b + c * d) - e / (f - g);
}

/* A relational over the arithmetic tiers: both sides are full
 * two-tier expressions, compared only when each is complete. */
int compare_mix(int a, int b, int c, int d, int e, int f)
{
  return a + b * c < d * e + f;
}

/* Every compound assignment, in turn, on a value passed in: each
 * must compute old OP b, store it, and the next must read back what
 * was stored. The shifts are by b, small enough to be defined, and
 * every value stays in the 16-bit range. Prints v after each. */
void comps(int a, int b)
{
  int v;
  v = a;
  v <<= b;
  puti(v);
  putln();
  v >>= b;
  puti(v);
  putln();
  v += b;
  puti(v);
  putln();
  v -= b;
  puti(v);
  putln();
  v *= b;
  puti(v);
  putln();
  v /= b;
  puti(v);
  putln();
  v %= b;
  puti(v);
  putln();
  v &= b;
  puti(v);
  putln();
  v |= b;
  puti(v);
  putln();
  v ^= b;
  puti(v);
  putln();
}

/* Compound assignments on array elements (a different code path
 * than on plain variables), and a post-decrement as the index: the
 * old i is the index, and i itself moves on for the last read. */
int comp_elem(int a, int b)
{
  int v[4];
  int i, r;
  for (i = 0; i < 4; i++)
    v[i] = a;
  v[1] += b;          // 10
  v[2] -= b;          // 4
  i = 3;
  v[i--] ^= a;        // the old i is the index: v[3] = 7^7 = 0
  r = v[0] + v[1] + v[2] + v[3];
  return r + i;       // i is 2 now: 21 + 2 = 23
}

/* A multiply the folder can't touch, for the wraparound tests. */
int wrapmul(int a, int b)
{
  return a * b;
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
  // Multi-level constants, folded at compile time: the parser's own
  // stack of partial results, at several tiers and nested deep.
  puti(1 + 2 * 3 - 4 / 2 % 3);      // 1+6-2 = 5
  putln();
  puti(5 + 6 * 7 - 8 / 3 % 4 ^ 1 | 2);  // 45^1|2 = 46
  putln();
  puti(2 + 3 * 4 < 5 * 6 + 7);      // 14 < 37, so 1
  putln();
  puti(1 + (2 + (3 + (4 + (5 + (6 + (7 + (8 + (9 + 10)))))))));
  putln();                          // 55: ten levels deep
  show_ops(37, 3);
  show_ops(-37, 3);
  // The same shapes at run time, where the folder can't touch them.
  puti(two_tiers(7, 6, 5, 10, 3, 2));     // 7+30-1 = 36
  putln();
  puti(two_tiers(-7, 6, 5, 10, 3, 2));    // -7+30-1 = 22
  putln();
  puti(four_tiers(7, 6, 5, 10, 3, 2, 40, 5));  // 36^40|5: 12|5 = 13
  putln();
  puti(four_tiers(-7, 6, 5, 10, 3, 2, 40, 5)); // 22^40|5 = 63
  putln();
  puti(right_nest(1, 2, 3, 4, 5, 6));    // 21
  putln();
  puti(right_nest(-1, -2, -3, -4, -5, -6));  // -21
  putln();
  puti(mixed(3, 4, 5, 2, 20, 6, 2));     // 3*14-20/4 = 37
  putln();
  puti(mixed(-3, 4, 5, 2, 20, 6, 2));    // -42-5 = -47
  putln();
  puti(compare_mix(7, 6, 5, 10, 3, 2));  // 37 < 32, so 0
  putln();
  puti(compare_mix(7, 6, 5, 40, 3, 2));  // 37 < 122, so 1
  putln();
  // The compound assignments, each on the value the last stored.
  comps(7, 3);      // 56 7 10 7 21 7 1 1 3 0
  comps(-7, 3);     // -56 -7 -4 -7 -21 -7 -1 3 3 0
  puti(comp_elem(7, 3));  // 23
  putln();
  // 16-bit wraparound: the exact value is computed on the host,
  // where it fits, and normalized with w16; ccish wraps itself, at
  // compile time (the constants) and at run time (wrapmul).
  puti(w16(30000 + 30000));     // 60000 wraps to -5536
  putln();
  puti(w16(wrapmul(20000, 4))); // 80000 wraps to 14464
  putln();
  puti(w16(-30000 - 30000));    // -60000 wraps to 5536
  putln();
  puti(w16(32767 + 1));         // -32768
  putln();
  puti(w16(-32768 - 1));        // 32767
  putln();
  puti(w16(1 << 15));           // 32768 wraps to -32768
  putln();
  puti(w16(wrapmul(-20000, 4))); // -80000 wraps to -14464
  putln();
  return 0;
}
