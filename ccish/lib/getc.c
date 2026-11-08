/* getc.c: C's getc over FILEIO, or CMDSYS's console. */
#include <stdio.h>
#include <plasma.h>

/* The next byte of f, or EOF at its end. The console has no end. */
int getc(FILE *f)
{
  char c;
  if ((int)f < 0)
    return __getc();
  return fileio.read((int)f, &c, 1) ? c : EOF;
}
