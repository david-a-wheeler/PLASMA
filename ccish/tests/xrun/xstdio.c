// xstdio.c: the library's stdio (lib/), against the host's: write a
// file with fwrite, fputs and putc; read it back with fread (whole
// items, then a short count) and getc, to EOF; reopen "wb" to truncate;
// remove it; fopen of a missing file; fflush; and the console's
// newline. The file is in the current directory.
#include <stdio.h>
#include <string.h>

#define TMP ",xstdio.tmp"

// Print n in decimal, then a space.
static void num(int n)
{
  char buf[8];
  int i = 0;
  if (n < 0) {
    putc('-', stdout);
    n = -n;
  }
  do {
    buf[i++] = (char)('0' + n % 10);
    n /= 10;
  } while (n);
  while (i)
    putc(buf[--i], stdout);
  putc(' ', stdout);
}

int main(void)
{
  FILE *f;
  char buf[16];
  int c, n;

  f = fopen(TMP, "wb");
  num(f != NULL);
  num((int)fwrite("abcdef", 1, 6, f));
  num((int)fwrite("ghij", 2, 2, f));
  num(fputs("kl\n", f) >= 0);       // any nonnegative value
  num(putc('m', f));
  num(fclose(f));
  fputs("\n", stdout);

  f = fopen(TMP, "rb");
  num((int)fread(buf, 2, 3, f));        // 3 items: abcdef
  buf[6] = 0;
  fputs(buf, stdout);
  putc(' ', stdout);
  num((int)fread(buf, 1, 3, f));        // ghi
  num((int)fread(buf, 4, 1, f));        // j k l \n
  num(buf[3] == '\n');
  num(getc(f));                         // 'm'
  num(getc(f) == EOF);
  num((int)fread(buf, 1, 4, f));
  fclose(f);
  fputs("\n", stdout);

  f = fopen(TMP, "wb");                 // truncates
  putc('z', f);
  fclose(f);
  f = fopen(TMP, "rb");
  n = 0;
  while ((c = getc(f)) != EOF)
    n++;
  num(n);
  fclose(f);
  num(remove(TMP));
  num(fopen(TMP, "rb") == NULL);
  num(fflush(stdout));
  fputs("two\nlines\n", stdout);
  putc('x', stdout);
  putc('\n', stdout);
  return 0;
}
