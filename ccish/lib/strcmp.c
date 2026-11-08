/* strcmp.c: C's strcmp. */
#include <string.h>

/* Compare a and b as unsigned chars (ccish's char is unsigned): < 0, 0
 * or > 0 as a sorts before, the same as, or after b. */
int strcmp(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}
