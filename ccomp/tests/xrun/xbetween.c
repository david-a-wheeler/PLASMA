/* Cross-test the betweenness transform (IR pass, irin.c). ccish rewrites
 * each "obvious" range test below to the one-add/one-compare IS_BETWEEN
 * shape; gcc compiles them literally. The two must print the same answer
 * for every x, which verifies the transform preserves the meaning,
 * including the signed wraparound it relies on. Each line is a run of 0
 * (out of range) and 1 (in range) for the x values shown, then a newline.
 * No function pointers (ccish has none); each check is written out. */
#include "ptest.h"

int gele(int x) { return (x >= 10) && (x <= 50); }
int ge_lt(int x) { return (x >= 10) && (x < 50); }
int gt_le(int x) { return (x > 10) && (x <= 50); }
int gt_lt(int x) { return (x > 10) && (x < 50); }
int neg(int x)  { return (x >= -5) && (x <= 5); }
int wide(int x) { return (x >= 32700) && (x <= 32767); }

/* Probe wide() at the wraparound edge. A loop up to 32767 is not portable
 * (x <= 32767 is always true for a 16-bit int, so it never exits under
 * ccish), so the edge is probed with fixed values: just below, at, and just
 * above the low bound, and at the top of the range. */
void probe(void)
{
  putc((char)('0' + wide(32699)));
  putc((char)('0' + wide(32700)));
  putc((char)('0' + wide(32701)));
  putc((char)('0' + wide(32766)));
  putc((char)('0' + wide(32767)));
  putln();
}

int main(void)
{
  int x;
  /* [5, 60]: covers both boundaries (10, 50) and the open ends. */
  for (x = 5; x <= 60; x++) putc((char)('0' + gele(x)));
  putln();
  for (x = 5; x <= 60; x++) putc((char)('0' + ge_lt(x)));
  putln();
  for (x = 5; x <= 60; x++) putc((char)('0' + gt_le(x)));
  putln();
  for (x = 5; x <= 60; x++) putc((char)('0' + gt_lt(x)));
  putln();
  /* [-10, 10]: a negative lower bound. */
  for (x = -10; x <= 10; x++) putc((char)('0' + neg(x)));
  putln();
  probe();
  return 0;
}
