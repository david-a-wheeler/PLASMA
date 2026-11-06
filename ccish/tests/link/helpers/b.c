/* The other half of helpers: see a.c. */
unsigned div_a(unsigned n, unsigned d);

unsigned div_b(unsigned n, unsigned d)
{
  return n / d;
}

int main()
{
  puti(div_a(60000u, 7));
  putc(' ');
  puti(div_b(50000u, 9));
  putln();
  return 0;
}
