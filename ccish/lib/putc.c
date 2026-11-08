/* putc.c: C's putc over FILEIO, or CMDSYS's console. */
#include <stdio.h>
#include <fileio.h>

/* Write c to f; c, or EOF if the write failed. On the console, '\n'
 * is CMDSYS's newline. */
int putc(int c, FILE *f)
{
  char b = (char)c;
  if ((int)f >= 0)
    return fileio.write((int)f, &b, 1) ? b : EOF;
  if (b == '\n')
    __putln();
  else
    __putc(b);
  return b;
}
