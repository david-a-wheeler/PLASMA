// rod.c: Rod's Colors, the lores demo from the Apple II DOS System
// Master disk, in C for ccish, written like src/samplesrc/rod.pla:
// the original's loops, plot by plot, through CONIO's grcolor, grplot
// and keypressed. On the Apple II those are the same 6502 routines
// rod.pla calls, so this compares ccish with PLASMA's compiler on the
// same work. Press any key to exit.
//
// This is the plainly written version. It reads like rod.pla, and
// uses char where values fit, as rod.pla uses byte. ccish's version
// is 1.19 times as fast (1.876 s a frame against rod.pla's 2.236),
// almost all from one C feature: "register".
//
// Declare a variable "register" when its address is never taken and
// its speed matters. C forbids taking a register variable's address,
// so no pointer, and no function called, can reach it, which lets
// ccish optimize more, mostly in loops. rodfast.c is the version
// where speed is all that matters.

#include "conio.h"

// The original's constants: the figure is SIZE by SIZE, i runs from 1
// to RINGS, j from 0 to J_MAX, and w from W_FIRST to W_LAST.
constexpr int SIZE = 40;
constexpr int MID = SIZE / 2;
constexpr int RINGS = MID - 1;
constexpr int J_MAX = MID - 1;
constexpr int W_FIRST = 3;
constexpr int W_LAST = 50;
constexpr int W_DIV = 12;
constexpr int Q_MUL = 3;
constexpr int Q_ADD = 3;

// The original's text: grmode's mixed mode, and the message's place.
constexpr int MIXED = 1;
constexpr int MSG_X = 11;
constexpr int MSG_Y = 1;

// Run Rod's Colors until a key is pressed.
int main(void)
{
  // Bytes, as rod.pla's: all fit, and a byte loads faster than a word.
  // We use "register" variables, which rod.pla can't say: no pointer
  // can reach register variables, so the CONIO calls can't change them
  // either. This permits ccish to compute i * w / W_DIV once for each i
  // instead of on every pass over j, and to end the j loop with a
  // cheaper != test (irloop.c: hoist() and ne_candidate()).
  register char w, i, j, k, fi, fk;

  conio.grmode(MIXED);
  conio.gotoxy(MSG_X, MSG_Y);
  __cmd_puts(pstr("Press any key to exit."));
  for (;;) {
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
    }
  }
done:
  conio.textmode(0);
  __cmd_puts(pstr("That's all, folks!"));
  __cmd_putln();
  return 0;
}
