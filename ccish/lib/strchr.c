/* strchr.c: C's strchr. */
#include <string.h>

/* The first c in s (its NUL counts), or NULL. */
char *strchr(const char *s, int c)
{
  for (;; s++) {
    if (*s == (char)c)
      return (char *)s;
    if (!*s)
      return NULL;
  }
}
