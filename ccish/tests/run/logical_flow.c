// && and || compiled as jumps (approach.md C4, "Conditions as jumps"):
// evaluation order and short-circuiting, in each statement that takes
// a condition, in ?:, as expression statements, as values, with
// constant operands, and in sizeof (not evaluated). t(n) and f(n)
// record n in a trace; show() prints the trace and clears it.
int trace;

// Record n in the trace, and return it (true unless 0).
int t(int n)
{
  trace = trace * 10 + n;
  return n;
}

// Record n in the trace, and return 0 (false).
int f(int n)
{
  trace = trace * 10 + n;
  return 0;
}

// Print the trace and a space, and clear it.
void show(void)
{
  __cmd_puti(trace);
  __cmd_putc(' ');
  trace = 0;
}

// Print Y if c is true, else N.
void yn(int c)
{
  __cmd_putc(c ? 'Y' : 'N');
}

// a && b, returned as a value.
int both(int a, int b)
{
  return a && b;
}

int main()
{
  int i, n, x;
  char ch;

  // if: short-circuiting and order.
  if (f(1) && t(2)) __cmd_putc('Y'); else __cmd_putc('N');
  show();
  if (t(1) || t(2)) __cmd_putc('Y'); else __cmd_putc('N');
  show();
  if (f(1) || t(2) && f(3) || t(4)) __cmd_putc('Y'); else __cmd_putc('N');
  show();
  if (t(1) && (f(2) || t(3)) && t(4)) __cmd_putc('Y'); else __cmd_putc('N');
  show();
  if (!(t(1) && f(2)) && !(f(3) || f(4))) __cmd_putc('Y'); else __cmd_putc('N');
  show();
  __cmd_putln();

  // As expression statements: only the side effects remain.
  t(1) && t(2);
  f(3) && t(4);
  show();
  f(1) || t(2);
  t(3) || t(4);
  show();
  __cmd_putln();

  // Loops.
  i = 0;
  while (i < 10 && i * i < 30)
    i++;
  __cmd_puti(i); __cmd_putc(' ');
  i = 0;
  do
    i++;
  while (i < 3 || i == 3);
  __cmd_puti(i); __cmd_putc(' ');
  for (i = 0; i < 10 && !(i > 2 && i % 2 == 1); i++)
    ;
  __cmd_puti(i); __cmd_putc(' ');
  n = 0;
  for (i = 0; i < 5 || n < 8; i++)
    n = n + 2;
  __cmd_puti(i); __cmd_putc(' ');
  __cmd_puti(n); __cmd_putc(' ');
  i = 0;
  while (!(i >= 4 || i == 2 && 0))
    i++;
  __cmd_puti(i); __cmd_putc(' ');
  __cmd_putln();

  // ?: with a && or || condition.
  __cmd_puti(t(1) && f(2) ? 10 : 20); __cmd_putc(' ');
  show();
  __cmd_puti(f(1) || t(2) ? 30 : 40); __cmd_putc(' ');
  show();
  __cmd_putln();

  // As values: always 1 or 0.
  x = t(3) && t(4);
  __cmd_puti(x); __cmd_putc(' ');
  show();
  x = f(0) || t(5);
  __cmd_puti(x); __cmd_putc(' ');
  show();
  x = !(t(1) || t(2));
  __cmd_puti(x); __cmd_putc(' ');
  show();
  __cmd_puti((t(1) && t(2)) + 2); __cmd_putc(' ');
  show();
  ch = t(1) || f(2);
  __cmd_puti(ch); __cmd_putc(' ');
  show();
  __cmd_puti(both(5, 6)); __cmd_putc(' ');
  __cmd_puti(both(5, 0)); __cmd_putc(' ');
  __cmd_putln();

  // Constant operands; a constant side that decides the result.
  yn(1 && t(1));
  yn(t(2) && 1);
  yn(0 || t(3));
  yn(t(4) || 0);
  show();
  if (t(1) && 0) __cmd_putc('Y'); else __cmd_putc('N');
  if (f(2) || 1) __cmd_putc('Y'); else __cmd_putc('N');
  show();
  if (0 && t(9)) __cmd_putc('Y'); else __cmd_putc('N');
  if (1 || t(9)) __cmd_putc('Y'); else __cmd_putc('N');
  show();
  __cmd_puti(sizeof(t(1) && t(2))); __cmd_putc(' ');
  show();
  __cmd_putln();
  return 0;
}
