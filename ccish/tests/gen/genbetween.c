/* genbetween.c - a byte betweenness (IR pass, irin.c): a char x tested
 * against two constants, whose range is a power-of-two block of bytes,
 * is one load, the misalignment r subtracted, the block's mask ANDed,
 * and one compare to the aligned base; the peephole stage makes the
 * general unaligned shape best, an r of 0 dropping the x - 0 and an r
 * of 1 a DECR. A true word, and a range that is not a power-of-two
 * block of bytes, keep the two comparisons. */
int aligned(char x)  { return x >= 48 && x <= 63; }   /* w 16, r 0 */
int misone(char x)   { return x >= 49 && x <= 56; }   /* w 8, r 1 */
int mistwo(char x)   { return x >= 50 && x <= 57; }   /* w 8, r 2 */
int branched(char x) { if (x >= 48 && x <= 63) return 1; return 0; }
int word(int x)      { return x >= 10 && x <= 50; }   /* a word: two compares */
int notpow2(char x)  { return x >= 49 && x <= 63; }   /* w 15: two compares */

int main(void)
{
  return aligned('5') + misone('5') + mistwo('5') + branched('5')
         + word(30) + notpow2('5') == 6;
}
