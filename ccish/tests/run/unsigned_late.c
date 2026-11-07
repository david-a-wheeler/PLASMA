/* Pointer comparisons are unsigned (approach.md 18.7, C): ccgen flips
 * both sign bits, the left operand's before the right operand's code.
 * When that code is too long for ccgen's IR buffer (here a sum of 100
 * x's, 199 IR ops), both are flipped afterward, the right one saved in
 * ccgen's temporary 1 meanwhile (frame offset 6, past main's two
 * locals and temporary 0). A short right operand is flipped in place. */
// listing-has: ENTER 8,0
// listing-has: SLW 6
// listing-has: LLW 6
int a[4];

int main()
{
  int x = 1, *p = a;
  __puti(p < a + (x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x - 99));
  __putc(' ');
  __puti(p >= a + (x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x - 99));
  __putc(' ');
  __puti(a + 2 > p);
  __putln();
  return 0;
}
