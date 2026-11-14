// A comma operator in an unevaluated operand of a constant expression
// is allowed (C11 6.6p3): sizeof's, and 0 &&'s right side.
enum { E = sizeof (1, 2), F = 0 && (1, 2) };

int main(void)
{
  __cmd_puti(E);
  __cmd_putc(' ');
  __cmd_puti(F);
  __cmd_putln();
  return 0;
}
