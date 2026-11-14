// sizeof of a comma expression (C11 6.5.17, 6.3.2.1p3): the array
// decays, so it's a pointer's size, 2; a parenthesized array alone is
// the array's, 8. sizeof doesn't evaluate it, so n stays 0.
int a[4];
int n;

int main(void)
{
  __cmd_puti(sizeof (n++, a));
  __cmd_putc(' ');
  __cmd_puti(sizeof (a));
  __cmd_putc(' ');
  __cmd_puti(n);
  __cmd_putln();
  return 0;
}
