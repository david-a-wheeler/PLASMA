// A three-state machine (red, green, yellow) driven by goto, run for a
// fixed number of ticks; it counts the ticks spent in each state. The
// dispatch is a chain of gotos and the loop is a goto loop (never
// counted). This is the pattern goto exists for. The host compiler is
// the oracle: the same source must print the same counts.
#include <ptest.h>
int main(void)
{
  int state = 0;        // 0 red, 1 green, 2 yellow
  int ticks = 0;
  int red = 0, green = 0, yellow = 0;
  int instate = 0;      // ticks in the current state, so far
  int dur[3];
  dur[0] = 5;           // red lasts 5 ticks
  dur[1] = 3;           // green lasts 3
  dur[2] = 1;           // yellow lasts 1
top:
  if (ticks >= 30)
    goto done;
  if (state == 0)
    goto do_red;
  if (state == 1)
    goto do_green;
  goto do_yellow;
do_red:
  red = red + 1;
  instate = instate + 1;
  if (instate >= dur[0]) { state = 1; instate = 0; }
  goto next;
do_green:
  green = green + 1;
  instate = instate + 1;
  if (instate >= dur[1]) { state = 2; instate = 0; }
  goto next;
do_yellow:
  yellow = yellow + 1;
  instate = instate + 1;
  if (instate >= dur[2]) { state = 0; instate = 0; }
  goto next;
next:
  ticks = ticks + 1;
  goto top;
done:
  puti(red); putc(' ');
  puti(green); putc(' ');
  puti(yellow); putc(' ');
  putln();
  return 0;
}
