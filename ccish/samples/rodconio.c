/* rodconio.c: Rod's Colors, the lores demo from the Apple II DOS System
 * Master disk, in C for ccish, written as src/samplesrc/rod.pla is:
 * the original's loops, plot by plot, through CONIO's grcolor, grplot
 * and keypressed. On the Apple II those are the same 6502 routines
 * rod.pla calls, so this compares ccish with PLASMA's compiler on the
 * same work. Press any key to exit. rodsimple.c plots in C instead;
 * rod.c is the optimized version.
 *
 * Build with -DROD_TEST=N (and lores.c with -DLORES_TEST) to run N
 * rounds of w from 3 to 50 under plvm. plvm's CONIO is C that prints
 * terminal escapes, so the test build defines its own CONIO table,
 * whose grcolor and grplot draw on lores.c's test screen; the program's
 * calls go through the table as they do on the Apple II. With
 * -DROD_SUMS as well, after each frame it prints a checksum of the
 * screen, as rod.c and rodref.c do. */
#include "lores.h"
#include "conio.h"

/* The original's constants: the figure is SIZE by SIZE, i runs from 1
 * to RINGS, j from 0 to J_MAX, and w from W_FIRST to W_LAST. */
constexpr int SIZE = 40;
constexpr int MID = SIZE / 2;
constexpr int RINGS = MID - 1;
constexpr int J_MAX = MID - 1;
constexpr int W_FIRST = 3;
constexpr int W_LAST = 50;
constexpr int W_DIV = 12;
constexpr int Q_MUL = 3;
constexpr int Q_ADD = 3;

/* The original's text: grmode's mixed mode, and the message's place. */
constexpr int MIXED = 1;
constexpr int MSG_X = 11;
constexpr int MSG_Y = 1;

#ifdef ROD_TEST
/* The test's CONIO: grcolor and grplot as the Apple's, on the test
 * screen; keypressed says no key. The rest aren't called. */
static int test_color;

/* Set the color for test_grplot, as CONIO's grcolor: its low nibble. */
static int test_grcolor(int c)
{
  test_color = c & (LORES_COLORS - 1);
  return 0;
}

/* Plot (x, y) in the color set, as CONIO's grplot. */
static int test_grplot(int x, int y)
{
  LORES_PLOT(x, y, test_color);
  return 0;
}

/* No key is ever pressed. */
static int test_keypressed(void)
{
  return 0;
}

struct t_conio conio = {
  test_keypressed, 0, 0, 0, 0, 0, 0, 0, 0, test_grcolor, test_grplot, 0, 0
};
#endif

/* Run Rod's Colors until a key is pressed (or, for the test, for
 * ROD_TEST rounds of w). */
int main(void)
{
  /* Bytes, as rod.pla's: all fit, and a byte loads faster than a word.
   * register, which rod.pla can't say: no pointer can reach them, so
   * ccgen computes i * w / W_DIV once for each i, not on every pass
   * over j, though the loop calls CONIO (irloop.c, hoist()). */
  register char w, i, j, k, fi, fk;
#ifdef ROD_TEST
  int round;

  for (round = 0; round < ROD_TEST; round++) {
#else
  conio.grmode(MIXED);
  conio.gotoxy(MSG_X, MSG_Y);
  __puts(pstr("Press any key to exit."));
  for (;;) {
#endif
    for (w = W_FIRST; w <= W_LAST; w++) {
      for (i = 1; i <= RINGS; i++) {
        for (j = 0; j <= J_MAX; j++) {
          k = i + j;
          fi = SIZE - i;
          fk = SIZE - k;
          conio.grcolor(j * Q_MUL / (i + Q_ADD) + i * w / W_DIV);
          conio.grplot(i, k);
          conio.grplot(k, i);
          conio.grplot(fi, fk);
          conio.grplot(fk, fi);
          conio.grplot(k, fi);
          conio.grplot(fi, k);
          conio.grplot(i, fk);
          conio.grplot(fk, i);
          if (conio.keypressed()) {
            conio.getkey();
            goto done;
          }
        }
      }
#ifdef ROD_SUMS
      lores_checksum(MID);
#endif
    }
  }
done:
#ifndef ROD_TEST
  conio.textmode(0);
  __puts(pstr("That's all, folks!"));
  __putln();
#endif
  return 0;
}
