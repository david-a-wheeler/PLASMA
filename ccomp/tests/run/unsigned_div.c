/* Unsigned /, % and >> by values known only at run time: calls to
 * ccgen's helper functions udiv, umod and ushr, which it writes into
 * the object once each (approach.md, "unsigned and helper functions
 * design"). Includes divisors of $8000 and more, which the helpers do
 * apart, and 1. showu prints in decimal, with / and % by 10 (helpers
 * too). Results by hand. */
// listing-count: 1 helper udiv:
// listing-count: 1 helper umod:
// listing-count: 1 helper ushr:
void showu(unsigned n)
{
  if (n >= 10)
    showu(n / 10);
  putc('0' + n % 10);
}

void show(unsigned n)
{
  showu(n);
  putc(' ');
}

int main()
{
  unsigned u = 40000, w = 65535, d = 3, d7 = 7, big = 40000, x = 0x8000;
  unsigned one = 1, s0 = 0, s1 = 1, s8 = 8, s15 = 15, s3 = 3, t = 30000;

  show(65535); show(40000); show(0);
  putln();
  show(u / d); show(u % d); show(w / d7); show(w % d7);
  putln();
  show(w / big); show(w % big); show(t / big); show(t % big);
  show(big / big); show(big % big); show(w / x); show(w % x);
  putln();
  show(w / one); show(w % one);
  putln();
  show(w >> s0); show(w >> s1); show(w >> s8); show(w >> s15);
  show(u >> s3);
  putln();
  u /= d; show(u);
  u %= 1000; show(u);
  s3 = 2;
  u >>= s3; show(u);
  putln();
  return 0;
}
