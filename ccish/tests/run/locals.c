// Local variables: declarations with and without initializers, several
// per declaration, initializers using earlier variables, block scope,
// shadowing, and assignment operators.
// f's frame: n (2 bytes) + a, b (4) + the larger of its two sibling
// blocks, which share frame space (x: 2; y, z and the inner a: 6) = 12.
// listing-has: ENTER 12,1
int f(int n)
{
  int a = n + 1, b;
  b = a * 2;
  {
    int x = 100;
    __puti(x + a); __putc(' ');
  }
  {
    int y = 1, z = 2;
    int a = 1000;               // shadows the outer a
    __puti(a + y + z); __putc(' ');
  }
  __puti(a); __putc(' ');             // the outer a again
  return b;
}

int main()
{
  int r;
  r = f(4);
  __puti(r); __putln();
  {
    int u, v, w;
    u = v = w = 7;              // right-associative
    __puti(u + v + w); __putc(' ');
    u += 3;  __puti(u); __putc(' ');
    u -= 4;  __puti(u); __putc(' ');
    u *= 5;  __puti(u); __putc(' ');
    u /= 4;  __puti(u); __putc(' ');
    u %= 4;  __puti(u); __putc(' ');
    u <<= 3; __puti(u); __putc(' ');
    u >>= 1; __puti(u); __putc(' ');
    u &= 10; __puti(u); __putc(' ');
    u |= 3;  __puti(u); __putc(' ');
    u ^= 15; __puti(u); __putc(' ');
    __puti(v = 3); __putc(' ');     // an assignment's value
    __puti((w) = v + 1); __putln(); // a parenthesized variable
  }
  return 0;
}
