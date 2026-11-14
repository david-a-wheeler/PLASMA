// The other half of helpers: see a.c.
unsigned div_a(unsigned n, unsigned d);

unsigned div_b(unsigned n, unsigned d)
{
  return n / d;
}

int main()
{
  __cmd_puti(div_a(60000u, 7));
  __cmd_putc(' ');
  __cmd_puti(div_b(50000u, 9));
  __cmd_putln();
  return 0;
}
