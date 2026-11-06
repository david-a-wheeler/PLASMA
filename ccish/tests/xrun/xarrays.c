/* xarrays.c — arrays: the sizeof a / sizeof a[0] idiom (portable
 * though sizeof is not), a folded constant size, element access,
 * pointer differences in elements, and p + i, which the peephole
 * fuses to one op. */
#include "ptest.h"

int sumto(int *v, int n)
{
  int i, s;
  s = 0;
  for (i = 0; i < n; i++)
    s += v[i];
  return s;
}

int main(void)
{
  int v[3 << 2];                    // 12 elements, a folded constant
  int a[3][4];
  int i, n;
  n = (int)(sizeof v / sizeof v[0]);
  puti(n);                          // 12 on both: the element count
  putln();
  for (i = 0; i < n; i++)
    v[i] = i * 3;
  puti(sumto(v, n));                // 198
  putln();
  puti(v[10] - v[9]);               // 3
  putln();
  puti((int)(&v[5] - &v[2]));       // 3: a pointer difference, elements
  putln();
  for (i = 0; i < 3; i++)
    for (n = 0; n < 4; n++)
      a[i][n] = i * 4 + n;
  puti(a[2][3]);                    // 11
  putln();
  puti(a[1][0] + a[1][1] + a[1][2] + a[1][3]);  // 22
  putln();
  return 0;
}
