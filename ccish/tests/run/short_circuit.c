// && || and ?: evaluate only what C says they do. t() prints a 't' each
// time it runs. Code that can never run is not compiled at all, so a
// division by zero there is not an error.
// listing-lacks: DIV
// listing-lacks: CB 99
int t(int v)
{
  __cmd_putc('t');
  return v;
}

void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

int main()
{
  int x = 1;
  show(t(0) && t(1)); show(t(1) && t(0)); show(t(1) || t(0));
  show(t(0) || t(2)); show(t(3) && t(4));
  __cmd_putln();
  // constant left sides: the right side never runs
  show(0 && t(99)); show(1 || t(99)); show(1 ? 5 : t(99));
  show(0 ? t(99) : 6);
  show(0 && 1 / 0); show(1 ? 2 : 1 / 0); show(0 ? 1 / 0 : 3);
  __cmd_putln();
  // void calls on both sides of ?:
  x ? __cmd_putc('y') : __cmd_putc('n');
  !x ? __cmd_putc('y') : __cmd_putc('n');
  __cmd_putln();
  return 0;
}
