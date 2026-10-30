/* Integer operators, precedence and associativity, with C semantics
 * for a 16-bit int. Division truncates toward zero, % takes the sign of
 * the dividend, and >> of a negative value is arithmetic (as in gcc).
 * C's ! must give 1, so it is NOT masked with ANDI 1. */
// listing-has: ANDI 1
void show(int n)
{
  puti(n);
  putc(' ');
}

int main()
{
  int a = 17, b = 5, n = -17;
  show(a + b); show(a - b); show(a * b); show(a / b); show(a % b);
  show(n / b); show(n % b); show(a / -5); show(a % -5);
  putln();
  show(a & b); show(a | b); show(a ^ b); show(a << 2); show(a >> 2);
  show(n >> 2); show(~a); show(-a); show(+a); show(!a); show(!0);
  show(!!b);
  putln();
  show(2 + 3 * 4); show((2 + 3) * 4); show(20 - 5 - 3); show(64 / 4 / 2);
  show(1 + 2 << 3); show(6 & 3 | 8); show(6 ^ 3 & 1); show(1 | 6 ^ 5);
  show(-2 * -3); show(- - 4); show(~0);
  putln();
  show(32767 + 1); show(-32768 - 1); show(300 * 300);
  putln();
  return 0;
}
