// The standard C names belong to the program: CMDSYS's functions are
// __cmd_puts, __cmd_memset and so on (approach.md decision 35), so a program
// can define puts and memset with C's meanings beside them. C's memset
// fills bytes (xxx), where CMDSYS's fills a 16-bit pattern (x, NUL, x);
// C's puts takes a C string and adds a newline, where CMDSYS's takes a
// PLASMA string and adds none.
// listing-lacks: __cmd_memset
// listing-has: __cmd_puts (import)
int puts(const char *s)
{
  while (*s)
    __cmd_putc(*s++);
  __cmd_putc('\n');
  return 0;
}

void *memset(void *p, int c, int n)
{
  char *q = p;
  while (n-- > 0)
    *q++ = (char)c;
  return p;
}

int main(void)
{
  char b[4];
  memset(b, 'x', 3);
  b[3] = 0;
  puts(b);
  puts("ok");
  __cmd_puts(pstr("pl"));
  __cmd_putln();
  return 0;
}
