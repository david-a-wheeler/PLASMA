/* strncat.c: C's strncat. */
#include <string.h>

/* Append at most n chars of s, then a NUL, to d; return d. */
char *strncat(char *d, const char *s, size_t n)
{
  char *p = d;
  while (*p)
    p++;
  for (; n && *s; n--)
    *p++ = *s++;
  *p = 0;
  return d;
}
