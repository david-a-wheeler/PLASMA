/* strncpy.c: C's strncpy. */
#include <string.h>

/* Copy at most n chars of s to d, then pad d with NULs to n; return
 * d. */
char *strncpy(char *d, const char *s, size_t n)
{
  char *p = d;
  for (; n && *s; n--)
    *p++ = *s++;
  __cmd_memset(p, 0, n);
  return d;
}
