/* Cross-test the byte betweenness transform (IR pass, irin.c). Where a
 * char x is tested against two constants whose range is a power-of-two
 * block of bytes, ccish rewrites the "obvious" range test to the
 * IS_BYTE_BETWEEN shape: the misalignment r subtracted, the block's mask
 * ANDed, and one compare to the aligned base; gcc compiles it
 * literally. Each case prints a line of 0 (out of range) and 1 (in
 * range) for every x it shows, and the two compilers' output must be
 * identical, which verifies the transform preserves the meaning. The
 * int cases no longer match the transform (a true word's mask may have
 * high bits set, which an ANDI could not keep); they stay as ordinary
 * code's cross-check. No function pointers (ccish has none); each check
 * is written out. */
#include "ptest.h"

/* The byte cases: x runs over all 256 bytes, one line each. */
int cb_al(char x)   { return (x >= 48) && (x <= 63); }  /* w 16, aligned */
int cb_r1(char x)   { return (x >= 49) && (x <= 56); }  /* w 8, r 1 */
int cb_r2(char x)   { return (x >= 50) && (x <= 57); }  /* w 8, r 2 */
int cb_open(char x) { return (x > 48) && (x < 57); }    /* open ends, w 8, r 1 */
int cb_all(char x)  { return (x >= 0) && (x <= 255); }  /* the whole byte */
int cb_one(char x)  { return (x >= 65) && (x <= 65); }  /* a singleton */
int cb_np2(char x)  { return (x >= 49) && (x <= 63); }  /* w 15: no transform */
int cb_out(char x)  { return (x >= 200) && (x <= 300); } /* not a byte range */

/* The int cases (no transform). */
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
  /* All 256 bytes, for each byte case. */
  for (x = 0; x < 256; x++) putc((char)('0' + cb_al((char)x)));
  putln();
  for (x = 0; x < 256; x++) putc((char)('0' + cb_r1((char)x)));
  putln();
  for (x = 0; x < 256; x++) putc((char)('0' + cb_r2((char)x)));
  putln();
  for (x = 0; x < 256; x++) putc((char)('0' + cb_open((char)x)));
  putln();
  for (x = 0; x < 256; x++) putc((char)('0' + cb_all((char)x)));
  putln();
  for (x = 0; x < 256; x++) putc((char)('0' + cb_one((char)x)));
  putln();
  for (x = 0; x < 256; x++) putc((char)('0' + cb_np2((char)x)));
  putln();
  for (x = 0; x < 256; x++) putc((char)('0' + cb_out((char)x)));
  putln();
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
