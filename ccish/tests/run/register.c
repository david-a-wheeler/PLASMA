// register (C11 6.7.1): an ordinary local, whose address C forbids
// taking (an error, tests/errors/register_addr.c), so ccgen is told no
// pointer reaches it (IR_REGLOCAL). In a block, in a for, with more
// than one declarator and an initializer, as a pointer through which
// the program reads and writes, and whose element's address is taken
// (&p[1] is p + 1, not p's address, so it's allowed).
char buf[4] = "abc";

int main()
{
  register int i, sum = 0;
  register char *p = buf;
  char *q;

  for (i = 0; i < 3; i++)
    sum += p[i];                    // 97 + 98 + 99
  q = &p[1];
  *q = 'B';
  for (register int j = 0; j < 3; j++)
    __cmd_putc(buf[j]);
  __cmd_putc(' ');
  __cmd_puti(sum); __cmd_putc(' ');
  {
    register char c = 'z';
    __cmd_putc(c);
  }
  __cmd_putln();
  return 0;
}
