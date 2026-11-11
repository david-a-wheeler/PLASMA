// name_cache.c - the recent-names cache in pp_intern() (ppname.c):
// the 16 names below all hash to the same one of its 16 slots, each
// used three times, so every use after the first is a same-slot hit
// that the pool's spelling must confirm (approach.md, "ccpp's name
// lookup"). A wrong id here would make a later use a new, undeclared
// name, and ccish's compile of this test would fail; the repeated
// sums pin the values a stale slot would disturb.
#include "ptest.h"

int main(void)
{
  int aa = 1, bw = 2, ce = 3, cu = 4, dk = 5, ei = 6, ey = 7, fo = 8,
      gm = 9, hc = 10, hs = 11, ia = 12, iq = 13, jg = 14, ke = 15,
      lk = 16, s;
  s = aa + bw + ce + cu + dk + ei + ey + fo + gm + hc + hs + ia +
      iq + jg + ke + lk;
  puti(s);
  putc(' ');
  s = lk + ke + jg + iq + ia + hs + hc + gm + fo + ey + ei + dk +
      cu + ce + bw + aa;
  puti(s);
  putc(' ');
  s = aa + bw + ce + cu + dk + ei + ey + fo + gm + hc + hs + ia +
      iq + jg + ke + lk;
  puti(s);
  putln();
  return 0;
}
