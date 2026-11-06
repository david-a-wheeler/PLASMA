/* xstr.c — the I/O itself: puts (which adds no newline, and which
 * takes only a pstr literal - see the contract in ptest.h), pstr,
 * putc, and a "string" built up byte by byte. */
#include "ptest.h"

int main(void)
{
  int i;
  puts(pstr("abc"));     // no newline added
  puti(7);
  putln();                // "abc7"
  puts(pstr("hi"));
  putc(33);
  putln();                // "hi!"
  // A runtime string: a char array is not a PLASMA string (it has
  // no length prefix), so print it byte by byte.
  putc('a');
  for (i = 1; i < 4; i++)
    putc('a' + i);
  putln();                // "abcd"
  return 0;
}
