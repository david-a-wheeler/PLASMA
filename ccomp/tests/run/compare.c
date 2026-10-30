/* Comparisons, !, && and || give C's 1 or 0 (not PLASMA's $FFFF), and
 * ?: picks a value. Constant cases are folded. */
void show(int n)
{
  puti(n);
  putc(' ');
}

int main()
{
  int a = 3, b = 5, n = -1;
  /* each comparison, true and false */
  show(a < b); show(a > b); show(a <= 3); show(a >= 4);
  show(a == 3); show(a != 3);
  putln();
  /* comparisons are signed */
  show(n < 0); show(n > a); show(-32768 < 32767); show(0x8000 < 0);
  putln();
  /* constants, and a constant on the left */
  show(2 < 3); show(3 == 4); show(0 < a); show(10 > a);
  putln();
  /* ! && || */
  show(!a); show(!0); show(a && b); show(a && 0); show(0 || b);
  show(a || n); show(5 && 7); show(0 || 0); show(n && !n);
  putln();
  /* ?:, including nested and constant conditions */
  show(a < b ? a : b); show(a > b ? a : b); show(1 ? 10 : 20);
  show(0 ? 10 : 20); show(a ? b ? 1 : 2 : 3); show(n < 0 ? -n : n);
  putln();
  return 0;
}
