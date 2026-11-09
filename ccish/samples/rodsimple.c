/* rodsimple.c: Rod's Colors, the lores demo from the Apple II DOS System
 * Master disk, in C for ccish, written plainly: the original's loops
 * (src/samplesrc/rod.bas and rod.pla), plot by plot, with lores.h's
 * LORES_PLOT. Press any key to exit. rod.c is the same program
 * optimized, about 8 times faster; comparing the two shows what ccish
 * does with plain code, and what it leaves to the programmer.
 *
 * Build with -DROD_TEST=N (and lores.c with -DLORES_TEST) to run N
 * rounds of w from 3 to 50 under plvm, with the screen an array; with
 * -DROD_SUMS as well, after each frame (each w) it prints a checksum
 * of the screen, as rod.c and rodref.c do. */
#include "lores.h"

#ifndef ROD_TEST
#include "conio.h"
#endif

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

/* The keyboard: KBD_READY is set when a key is waiting; reading the
 * strobe clears it. */
#define KBD     (*(volatile char *)0xC000)
#define KBDSTRB (*(volatile char *)0xC010)
constexpr int KBD_READY = 1 << 7;

#ifdef ROD_SUMS
constexpr int HASH_MUL = 3;     /* as rodref.c's */

/* Print a checksum of the figure's text rows, as rodref.c does. */
static void checksum(void)
{
  int y, x;
  unsigned h;

  h = 0;
  for (y = 0; y < SIZE; y += 2)
    for (x = 0; x < LORES_WIDTH; x++)
      h = h * HASH_MUL + lores_row[y][x];
  __puti((int)h);
  __putln();
}
#endif

/* Run Rod's Colors until a key is pressed (or, for the test, for
 * ROD_TEST rounds of w). */
int main(void)
{
  int w, i, j, k, fi, fk, c;
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
          c = (j * Q_MUL / (i + Q_ADD) + i * w / W_DIV)
              & (LORES_COLORS - 1);
          fi = SIZE - i;
          fk = SIZE - k;
          LORES_PLOT(i, k, c);
          LORES_PLOT(k, i, c);
          LORES_PLOT(fi, fk, c);
          LORES_PLOT(fk, fi, c);
          LORES_PLOT(k, fi, c);
          LORES_PLOT(fi, k, c);
          LORES_PLOT(i, fk, c);
          LORES_PLOT(fk, i, c);
#ifndef ROD_TEST
          if (KBD & KBD_READY)
            goto done;
#endif
        }
      }
#ifdef ROD_SUMS
      checksum();
#endif
    }
  }
#ifndef ROD_TEST
done:
  KBDSTRB;
  conio.textmode(0);
  __puts(pstr("That's all, folks!"));
  __putln();
#endif
  return 0;
}
