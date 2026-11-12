/* xltest.c: XLOAD's test program (tests/special/xload.sh), which holds
 * what an image can: addresses of functions, strings and data in its
 * data; a zero region (small and large globals, a static local and a
 * static global, each read as zero before it's written); calls through
 * function pointers; a helper (unsigned division); imports from CMDSYS
 * and FILEIO (whose table it reads its own image's magic through);
 * main(argc, argv), so the library's startup, which splits the command
 * line XLOAD left; and an exit status. */
#include <fileio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int (*ops[2])(int, int) = {add, sub};
char *words[2] = {"zero", "one"};
int table[3] = {10, 20, 30};
int *mid = &table[1];
int zeros[200];
char big[700];
static int seen;

/* Print C string s. */
void cputs(char *s)
{
  while (*s)
    __putc(*s++);
}

/* a / b, unsigned: a helper. */
unsigned quot(unsigned a, unsigned b)
{
  return a / b;
}

/* How many times it's been called. */
int count(void)
{
  static int n;
  return ++n;
}

int main(int argc, char **argv)
{
  int i, ref;
  char path[17], buf[2];
  __puti(argc);
  __putc(' ');
  for (i = 0; i < argc; i++) {
    cputs(argv[i]);
    __putc(' ');
  }
  __puti(ops[0](2, 3));
  __putc(' ');
  __puti(ops[1](2, 3));
  __putc(' ');
  cputs(words[1]);
  __putc(' ');
  __puti(*mid);
  __putc(' ');
  __puti(zeros[199] + big[699] + seen + count());
  __putc(' ');
  big[699] = 5;
  zeros[0] = 6;
  seen = 7;
  __puti(big[699] + zeros[0] + seen + count());
  __putc(' ');
  __puti((int)quot(50000u, 7u));
  __putc(' ');
  for (i = 0; argv[0][i] && i < 16; i++)
    path[i + 1] = argv[0][i];
  path[0] = (char)i;
  ref = fileio.open(path);
  buf[0] = buf[1] = '?';
  if (ref) {
    fileio.read(ref, buf, 2);
    fileio.close(ref);
  }
  __putc(buf[0]);
  __putc(buf[1]);
  __putln();
  return 7;
}
