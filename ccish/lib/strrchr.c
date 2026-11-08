/* strrchr.c: C's strrchr. */
#include <string.h>

/* The last c in s (its NUL counts), or NULL. */
char *strrchr(const char *s, int c)
{
  const char *r = NULL;
  for (;; s++) {
    if (*s == (char)c)
      r = s;
    if (!*s)
      return (char *)r;
  }
}
