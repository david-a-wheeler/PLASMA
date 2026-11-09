/* rodref.c: the reference for rod.c's test, for gcc, not ccish. It
 * runs the original Rod's Colors (src/samplesrc/rod.bas) plot by plot
 * on a simulated lores screen, and after each pass over i prints the
 * checksum that rod.c built with -DROD_TEST prints.
 * Usage: rodref PASSES (passes over w, as rod.c's ROD_TEST). */
#include <stdio.h>
#include <stdlib.h>

static unsigned char mem[1024];   /* $400 to $7FF */

/* Text row r's offset from $400. */
static int row(int r)
{
  return (r & 7) * 128 + (r >> 3) * 40;
}

/* Applesoft's PLOT x, y in COLOR= c. */
static void plot(int x, int y, int c)
{
  unsigned char *p = &mem[row(y >> 1) + x];

  c &= 15;
  if (y & 1)
    *p = (*p & 0x0F) | (c << 4);
  else
    *p = (*p & 0xF0) | c;
}

/* The checksum of lores rows 0 to 39, as rod.c's checksum(). */
static void checksum(void)
{
  unsigned h = 0;
  int r, x;

  for (r = 0; r < 20; r++)
    for (x = 0; x < 40; x++)
      h = (h * 3 + mem[row(r) + x]) & 0xFFFF;
  printf("%d\n", (int)(short)h);
}

/* Run PASSES passes of the original. */
int main(int argc, char **argv)
{
  int passes = argc > 1 ? atoi(argv[1]) : 1;
  int pass, w, i, j, k, c;

  for (pass = 0; pass < passes; pass++)
    for (w = 3; w <= 50; w++) {
      for (i = 1; i <= 19; i++) {
        for (j = 0; j <= 19; j++) {
          k = i + j;
          c = (j * 3) / (i + 3) + i * w / 12;
          plot(i, k, c);
          plot(k, i, c);
          plot(40 - i, 40 - k, c);
          plot(40 - k, 40 - i, c);
          plot(k, 40 - i, c);
          plot(40 - i, k, c);
          plot(i, 40 - k, c);
          plot(40 - k, i, c);
        }
      }
      checksum();
    }
  return 0;
}
