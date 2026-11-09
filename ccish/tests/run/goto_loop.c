// A backward goto makes a loop, as an enclosing for does: a counted
// loop after a goto label is treated as nested (irloop.c, cl_nested),
// so a store through a pointer in its body keeps it from being counted.
// Here the second run's *p = 8 sets i, through the address taken after
// the loop: C runs i = 0, 1, 2, 3, then 9, five passes, where a counted
// loop, keeping its count on the stack, would run ten.
int main()
{
  int i, n = 0, runs = 0, passes = 0, *p = &n;

again:
  for (i = 0; i < 10; i++) {
    passes++;
    if (i == 3)
      *p = 8;
  }
  runs++;
  __puti(passes); __putc(' ');
  if (runs < 2) {
    p = &i;
    passes = 0;
    goto again;
  }
  __puti(n); __putln();
  return 0;
}
