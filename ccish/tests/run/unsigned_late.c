// Pointer comparisons are unsigned (approach.md 18.7, C): each calls
// CMDSYS's ISULT or the rest (irin.c), however long the right
// operand's code (here a sum of 100 x's, 199 IR ops, longer than
// ccgen's IR buffer, which a sign-bit flip once had to reach past).
// listing-count: 1 __cmd_isult (import)
// listing-count: 1 __cmd_isuge (import)
int a[4];

int main()
{
  int x = 1, *p = a;
  __cmd_puti(p < a + (x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x - 99));
  __cmd_putc(' ');
  __cmd_puti(p >= a + (x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x - 99));
  __cmd_putc(' ');
  __cmd_puti(a + 2 > p);
  __cmd_putln();
  return 0;
}
