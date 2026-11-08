/* strcpy.c: C's strcpy. */
#include <string.h>

/* Copy s, with its NUL, to d; return d. */
char *strcpy(char *d, const char *s)
{
  char *p = d;
  while ((*p++ = *s++) != 0)
    ;
  return d;
}
