/* xloops.c — the shapes of loop update the peephole must handle:
 * i++, ++i, i = i + 1, i += 2, i -= 1, while, do-while, nested
 * loops, break and continue, and an update whose old value is used. */
#include "ptest.h"

int main(void)
{
  int i, s, n;
  s = 0;
  for (i = 0; i < 10; i++)        // 45
    s += i;
  puti(s);
  putln();
  s = 0;
  for (i = 0; i < 10; ++i)        // 45
    s += i;
  puti(s);
  putln();
  s = 0;
  for (i = 0; i < 10; i = i + 1)  // 45
    s += i;
  puti(s);
  putln();
  s = 0;
  for (i = 0; i < 10; i += 2)     // 0+2+4+6+8 = 20
    s += i;
  puti(s);
  putln();
  s = 0;
  for (i = 9; i > 0; i -= 1)      // 45
    s += i;
  puti(s);
  putln();
  s = 0;
  i = 0;
  while (i < 5) {
    s += i;
    i = i + 1;
  }
  puti(s);                        // 10
  putln();
  s = 0;
  i = 5;
  do {
    s += i;
    i -= 1;
  } while (i >= 1);
  puti(s);                        // 15
  putln();
  s = 0;
  for (i = 0; i < 6; i++) {
    if (i == 3)
      continue;
    if (i == 5)
      break;
    s += i;
  }
  puti(s);                        // 0+1+2+4 = 7
  putln();
  s = 0;
  for (i = 0; i < 3; i++)
    for (n = 0; n < 4; n++)
      s += 1;
  puti(s);                        // 12
  putln();
  s = 0;
  for (i = 0; i < 4; i++)
    s += i++;                     // old value used: 0+1+2+3 = 6
  puti(s);
  putln();
  return 0;
}
