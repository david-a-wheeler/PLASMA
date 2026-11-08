/* strcat.c: C's strcat. */
#include <string.h>

/* Append s, with its NUL, to d; return d. */
char *strcat(char *d, const char *s)
{
  char *p = d;
  while (*p)
    p++;
  while ((*p++ = *s++) != 0)
    ;
  return d;
}
