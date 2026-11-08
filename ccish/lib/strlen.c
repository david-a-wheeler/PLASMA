/* strlen.c: C's strlen. */
#include <string.h>

/* The length of s, not counting its NUL. */
size_t strlen(const char *s)
{
  const char *p = s;
  while (*p)
    p++;
  return p - s;
}
