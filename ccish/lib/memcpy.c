/* memcpy.c: C's memcpy, through CMDSYS's. */
#include <string.h>

/* Copy n bytes from s to d, which don't overlap; return d. */
void *memcpy(void *d, const void *s, size_t n)
{
  __memcpy(d, s, n);
  return d;
}
