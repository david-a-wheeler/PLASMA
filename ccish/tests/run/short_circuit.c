/* && || and ?: evaluate only what C says they do. t() prints a 't' each
 * time it runs. Code that can never run is not compiled at all, so a
 * division by zero there is not an error. */
// listing-lacks: DIV
// listing-lacks: CB 99
int t(int v)
{
  __putc('t');
  return v;
}

void show(int n)
{
  __puti(n);
  __putc(' ');
}

int main()
{
  int x = 1;
  show(t(0) && t(1)); show(t(1) && t(0)); show(t(1) || t(0));
  show(t(0) || t(2)); show(t(3) && t(4));
  __putln();
  /* constant left sides: the right side never runs */
  show(0 && t(99)); show(1 || t(99)); show(1 ? 5 : t(99));
  show(0 ? t(99) : 6);
  show(0 && 1 / 0); show(1 ? 2 : 1 / 0); show(0 ? 1 / 0 : 3);
  __putln();
  /* void calls on both sides of ?: */
  x ? __putc('y') : __putc('n');
  !x ? __putc('y') : __putc('n');
  __putln();
  return 0;
}
