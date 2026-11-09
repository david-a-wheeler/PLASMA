// xandi.c - the ANDI fold (ANDI m ANDI n -> ANDI (m & n)): a (char)
// cast on an int (ANDI 255) followed by a small & mask (ANDI n) merges
// to ANDI (255 & n), which is just the mask. The cross-check runs this
// against the host compiler: the value must be x's low byte & the mask,
// whether x is a small byte, a byte at or above 128 (the sign edge), or
// a value with a high byte that the cast clears.
#include "ptest.h"

int fold(int x)
{
  return (int)((char)(x) & 240);
}

int fold3(int x)
{
  return (int)((char)(x) & 240 & 192);
}

int main(void)
{
  int x;
  for (x = 0; x < 256; x++)
  {
    puti(w16(fold(x))); putc(' ');
    puti(w16(fold3(x))); putc(' ');
  }
  for (x = 256; x < 512; x += 16)
  {
    puti(w16(fold(x))); putc(' ');
    puti(w16(fold3(x))); putc(' ');
  }
  putln();
  return 0;
}
