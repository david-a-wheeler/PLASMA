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
    __cmd_puti(x + a); __cmd_putc(' ');
  }
  {
    int y = 1, z = 2;
    int a = 1000;               // shadows the outer a
    __cmd_puti(a + y + z); __cmd_putc(' ');
  }
  __cmd_puti(a); __cmd_putc(' ');             // the outer a again
  return b;
}

int main()
{
  int r;
  r = f(4);
  __cmd_puti(r); __cmd_putln();
  {
    int u, v, w;
    u = v = w = 7;              // right-associative
    __cmd_puti(u + v + w); __cmd_putc(' ');
    u += 3;  __cmd_puti(u); __cmd_putc(' ');
    u -= 4;  __cmd_puti(u); __cmd_putc(' ');
    u *= 5;  __cmd_puti(u); __cmd_putc(' ');
    u /= 4;  __cmd_puti(u); __cmd_putc(' ');
    u %= 4;  __cmd_puti(u); __cmd_putc(' ');
    u <<= 3; __cmd_puti(u); __cmd_putc(' ');
    u >>= 1; __cmd_puti(u); __cmd_putc(' ');
    u &= 10; __cmd_puti(u); __cmd_putc(' ');
    u |= 3;  __cmd_puti(u); __cmd_putc(' ');
    u ^= 15; __cmd_puti(u); __cmd_putc(' ');
    __cmd_puti(v = 3); __cmd_putc(' ');     // an assignment's value
    __cmd_puti((w) = v + 1); __cmd_putln(); // a parenthesized variable
  }
  return 0;
}
