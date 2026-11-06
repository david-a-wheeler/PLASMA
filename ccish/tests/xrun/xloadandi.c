/* xloadandi.c - the byte-load + ANDI 255 fold: a byte load (LLB) already
 * zero-extends, so c & 255 is just c, and the cross-check requires the
 * folded code to agree with the host's unfolded c & 255. notbyte (a word
 * load, LLW) is the control: x & 255 keeps the ANDI and gives the low
 * byte, which the second loop checks for values above 255. */
#include "ptest.h"

int loc(char c)   { return c & 255; }
int notbyte(int x){ return x & 255; }

int main(void)
{
  int x;
  for (x = 0; x < 64; x++)
  {
    puti(w16(loc((char)x))); putc(' ');
    puti(w16(notbyte(x))); putc(' ');
  }
  for (x = 300; x < 316; x++)
  {
    puti(w16(loc((char)x))); putc(' ');
    puti(w16(notbyte(x))); putc(' ');
  }
  putln();
  return 0;
}
