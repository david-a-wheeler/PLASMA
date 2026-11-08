/* fputs.c: C's fputs over FILEIO, or CMDSYS's console. */
#include <stdio.h>
#include <string.h>
#include <plasma.h>

/* Write s to f; 0, or EOF if the write failed. On the console, '\n' is
 * CMDSYS's newline. */
int fputs(const char *s, FILE *f)
{
  size_t n;
  if ((int)f < 0) {
    for (; *s; s++)
      if (*s == '\n')
        __putln();
      else
        __putc(*s);
    return 0;
  }
  n = strlen(s);
  return fileio.write((int)f, (char *)s, n) == n ? 0 : EOF;
}
