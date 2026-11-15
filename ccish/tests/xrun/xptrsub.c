// xptrsub.c: pointer subtraction when the element is bigger than two
// bytes (pexpr.c: the difference in bytes is divided by the element
// size, not shifted for a two-byte element, and not used raw for a
// one-byte one). The elements are arrays: a row of four ints (eight
// bytes, a power of two) and a row of three chars (three bytes, not a
// power of two), so a shift instead of a division, or a wrong size,
// shows. The difference is in elements, as C's is (6.5.6p6), and the
// operands need not be the array itself: two other points in it, and
// the subtraction reversed, go the other way.
#include "ptest.h"

int rows[5][4];
char cells[10][3];

int main(void)
{
  int (*p)[4] = rows;
  char (*q)[3] = cells;
  int i;
  for (i = 0; i < 20; i++)
    rows[i / 4][i % 4] = i + 1;
  for (i = 0; i < 30; i++)
    cells[i / 3][i % 3] = (char)(i + 1);
  puti((p + 2) - p);        // 2
  putln();
  puti((p + 4) - (p + 1));  // 3
  putln();
  puti(p - (p + 3));        // -3
  putln();
  puti((q + 3) - q);        // 3
  putln();
  puti((q + 9) - (q + 4));  // 5
  putln();
  puti(rows[0][0] + rows[4][3]);  // 1 + 20, the arrays intact
  putln();
  return 0;
}
