/* genbetween.c - a betweenness (IR pass, irin.c): the same local
 * loaded twice, each time compared against a constant, whose range is
 * a power-of-two block, is one load, the misalignment r subtracted,
 * the block's mask ANDed, and one compare to the aligned base; the
 * peephole stage makes the general unaligned shape best, an r of 0
 * dropping the x - 0 and an r of 1 a DECR. A char local (0 to 255
 * when loaded) matches with a power-of-two block of bytes; an int
 * local matches with a power-of-two block of words (low <= high
 * signed, the width a power of two from 1 to 65536), or with each
 * load masked to its low byte (the (char) cast), which is a byte
 * again; a range that is neither keeps the two comparisons. */
int aligned(char x)   { return x >= 48 && x <= 63; }  /* w 16, r 0 */
int misone(char x)    { return x >= 49 && x <= 56; }  /* w 8, r 1 */
int mistwo(char x)    { return x >= 50 && x <= 57; }  /* w 8, r 2 */
int branched(char x)  { if (x >= 48 && x <= 63) return 1; return 0; }
int word(int x)       { return x >= 10 && x <= 50; }  /* w 41: two compares */
int notpow2(char x)   { return x >= 49 && x <= 63; }  /* w 15: two compares */
int waligned(int x)   { return x >= 16 && x <= 31; }  /* w 16, r 0 */
int wmisone(int x)    { return x >= 17 && x <= 32; }  /* w 16, r 1 */
int wneg(int x)       { return x >= -8 && x <= 7; }   /* w 16, r 8 */
int wbig(int x)       { return x >= -16384 && x <= 16383; }  /* w 32768 */
int wwhole(int x)     { return x >= -32768 && x <= 32767; }  /* w 65536 */
int wempty(int x)     { return x > 32767 && x <= 32767; }    /* empty */
int wcast(int x)      { return (char)x >= 48 && (char)x <= 63; }
int wcastmis(int x)   { return (char)x >= 10 && (char)x <= 25; }  /* base 0 */

int main(void)
{
  return aligned('5') + misone('5') + mistwo('5') + branched('5')
         + word(30) + notpow2('5')
         + waligned(20) + wmisone(20) + wneg(0)
         + wbig(-16384) + wbig(16383) + wwhole(12345) + wempty(5)
         + wcast('5') + wcastmis(30) == 11;
}
