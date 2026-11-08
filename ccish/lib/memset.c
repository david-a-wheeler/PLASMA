/* memset.c: C's memset, through CMDSYS's, which fills with a 16-bit
 * pattern. */
#include <string.h>

/* Set n bytes at d to c; return d. */
void *memset(void *d, int c, size_t n)
{
  __memset(d, (c & 255) | c << 8, n);
  return d;
}
