/* Cross-test the betweenness transform (IR pass, irin.c). Where the same
 * local, loaded twice, is tested against two constants whose range is a
 * power-of-two block, ccish rewrites the "obvious" range test to the mask
 * shape: the misalignment r subtracted, the block's mask ANDed, and one
 * compare to the aligned base; gcc compiles it literally. A char local
 * (0 to 255) matches with a block of bytes; an int local matches with a
 * block of words (width a power of two, low <= high signed) or with each
 * load masked to its low byte (the (char) cast), which is a byte again.
 * Each case prints a line of 0 (out of range) and 1 (in range) for every
 * x it shows, and the two compilers' output must be identical, which
 * verifies the transform preserves the meaning. The byte cases run over
 * all 256 bytes; the int cases sweep the whole 16-bit space (minus the
 * top, which a probe covers, since a loop to 32767 would not exit under
 * ccish: x <= 32767 is always true for a 16-bit int). No function
 * pointers (ccish has none); each check is written out. */
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

/* The int cases that do not match the transform. */
int gele(int x) { return (x >= 10) && (x <= 50); }
int ge_lt(int x) { return (x >= 10) && (x < 50); }
int gt_le(int x) { return (x > 10) && (x <= 50); }
int gt_lt(int x) { return (x > 10) && (x < 50); }
int neg(int x)  { return (x >= -5) && (x <= 5); }
int wide(int x) { return (x >= 32700) && (x <= 32767); }

/* The int cases that now match the transform: power-of-two blocks of
 * words, each swept over the whole 16-bit space, one line each. */
int wi_al(int x)   { return (x >= 16) && (x <= 31); }  /* w 16, r 0 */
int wi_r1(int x)   { return (x >= 17) && (x <= 32); }  /* w 16, r 1 */
int wi_r10(int x)  { return (x >= 10) && (x <= 25); }  /* w 16, r 10, base 0 */
int wi_neg(int x)  { return (x >= -8) && (x <= 7); }   /* w 16, r 8 */
int wi_one(int x)  { return (x >= 200) && (x <= 200); } /* w 1 */
int wi_mid(int x)  { return (x >= 800) && (x <= 1311); } /* w 512 */
int wi_half(int x) { return (x >= -16384) && (x <= 16383); } /* w 32768 */
int wi_top(int x)  { return (x >= 32752) && (x <= 32767); }  /* w 16, the top */

/* The (char) cast on an int: the same block, a byte, tested on the
 * masked low byte. (char) is unsigned in both compilers (ptest.h), so
 * the mask agrees. */
int wc_al(int x)   { return (char)x >= 48 && (char)x <= 63; }
int wc_r10(int x)  { return (char)x >= 10 && (char)x <= 25; } /* base 0 */

/* Probe wide() and wi_top() at the wraparound edge. A loop up to 32767
 * is not portable (x <= 32767 is always true for a 16-bit int, so it
 * never exits under ccish), so the edge is probed with fixed values:
 * just below, at, and just above each bound, and at the top of the
 * range. */
void probe(void)
{
  putc((char)('0' + wide(32699)));
  putc((char)('0' + wide(32700)));
  putc((char)('0' + wide(32701)));
  putc((char)('0' + wide(32766)));
  putc((char)('0' + wide(32767)));
  putc((char)('0' + wi_top(32751)));
  putc((char)('0' + wi_top(32752)));
  putc((char)('0' + wi_top(32753)));
  putc((char)('0' + wi_top(32766)));
  putc((char)('0' + wi_top(32767)));
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
  /* The whole 16-bit space, minus the top, for each int case. */
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wi_al(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wi_r1(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wi_r10(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wi_neg(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wi_one(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wi_mid(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wi_half(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wi_top(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wc_al(x)));
  putln();
  for (x = -32768; x <= 32766; x++) putc((char)('0' + wc_r10(x)));
  putln();
  probe();
  return 0;
}
