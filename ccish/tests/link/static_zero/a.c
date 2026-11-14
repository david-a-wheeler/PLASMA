// Two objects, each with a zero area (its static locals with no
// initializer): cclink gives each its own, so a's and b's counters and
// buffers are apart, and each starts at zero.
int bump_b(void);
int fill_b(int c);

int bump_a(void)
{
  static int n;
  return ++n;
}

int fill_a(int c)
{
  static char buf[8];
  buf[7] = (char)c;
  return buf[0] + buf[7];
}

int main(void)
{
  __cmd_puti(bump_a()); __cmd_putc(' ');
  __cmd_puti(bump_a()); __cmd_putc(' ');
  __cmd_puti(bump_b()); __cmd_putc(' ');
  __cmd_puti(fill_a(5)); __cmd_putc(' ');
  __cmd_puti(fill_b(6)); __cmd_putc(' ');
  __cmd_puti(bump_a()); __cmd_putc(' ');
  __cmd_puti(bump_b()); __cmd_putln();
  return 0;
}
