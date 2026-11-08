/* memmove.c: C's memmove. CMDSYS's memcpy copies overlapping blocks on
 * an Apple II, but plvm's calls the host's memcpy, which needn't, so
 * this copies a byte at a time. */
#include <string.h>

/* Copy n bytes from s to d, which may overlap; return d. */
void *memmove(void *d, const void *s, size_t n)
{
  char *p = d;
  const char *q = s;
  if (p <= q)
    for (; n; n--)
      *p++ = *q++;
  else {
    p += n;
    q += n;
    for (; n; n--)
      *--p = *--q;
  }
  return d;
}
