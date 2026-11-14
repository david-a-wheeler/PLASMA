// &&, || and ! compiled as jumps (approach.md C4, "Conditions as
// jumps") must agree with C, as conditions and as values (1 or 0), for
// operands that are 0 or another value (7, -1, 2). Each line: a, b
// and c as 0 or 1, then ten conditions' bits, then the same ten as
// values.

// The ten conditions, each tested by an if: bit k set if the k-th is
// true.
int as_cond(int a, int b, int c)
{
  int r = 0;
  if (a && b) r = r + 1;
  if (a || b) r = r + 2;
  if (!(a && b)) r = r + 4;
  if (!(a || b)) r = r + 8;
  if (a && b || c) r = r + 16;
  if (a || b && c) r = r + 32;
  if (a && (b || c)) r = r + 64;
  if ((a || b) && (b || c)) r = r + 128;
  if (!(a && !b) && c) r = r + 256;
  if (a ? b && c : b || c) r = r + 512;
  return r;
}

// The same ten, as values: each must be 1 or 0.
int as_value(int a, int b, int c)
{
  return (a && b) + 2 * (a || b) + 4 * !(a && b) + 8 * !(a || b)
         + 16 * (a && b || c) + 32 * (a || b && c)
         + 64 * (a && (b || c)) + 128 * ((a || b) && (b || c))
         + 256 * (!(a && !b) && c) + 512 * (a ? b && c : b || c);
}

int main()
{
  int i, a, b, c;
  for (i = 0; i < 8; i++) {
    a = i & 4 ? 7 : 0;
    b = i & 2 ? -1 : 0;
    c = i & 1 ? 2 : 0;
    __cmd_puti(a != 0); __cmd_putc(' ');
    __cmd_puti(b != 0); __cmd_putc(' ');
    __cmd_puti(c != 0); __cmd_putc(' ');
    __cmd_puti(as_cond(a, b, c)); __cmd_putc(' ');
    __cmd_puti(as_value(a, b, c));
    __cmd_putln();
  }
  return 0;
}
