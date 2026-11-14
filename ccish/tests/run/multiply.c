// Multiplying by some constants uses cheaper ops than MUL, as PLASMA's
// own optimizer does: * 2 is DUP ADD (faster than CN 1 SHL), * 3 is
// DUP IDXW, * 1 is nothing, a larger power of two is a shift, and * 0
// still computes its operand (g() is called) but gives 0. Other
// constants, such as 5, still use MUL.
// listing-count: 1 MUL
// listing-count: 3 SHL
// listing-count: 1 IDXW
// listing-count: 2 DUP
void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

int g()
{
  __cmd_putc('g');
  return 5;
}

void f(int x)
{
  show(x * 2);
  show(x * 3);
  show(x * 4);
  show(x * 16);
  show(x * 0);
  show(x * 1);
  show(x * 0x4000);
  show(x * 5);
}

int main()
{
  f(3);
  __cmd_putln();
  f(-7);
  __cmd_putln();
  show(g() * 0);
  __cmd_putln();
  return 0;
}
