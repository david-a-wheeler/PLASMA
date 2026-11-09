/* genret.c - a jump to a return is that return (irin.c, jump_return):
 * LEAVE n is a byte smaller than BRNCH, and the jump it saves never
 * runs. The stack at a jump is the same as at its label, so the value
 * a ?: or && leaves is the one returned. Shapes: a ?: and an && as the
 * returned value; an if/else-if chain at a void function's end, whose
 * branches each jump to the end; and a return inside a counted loop,
 * where the loop's values are dropped first (irloop.c, replay_body),
 * so the jump to that code isn't to a return and stays. */
int cond(int c, int a, int b)
{
  return c ? a : b;
}

int both(int a, int b)
{
  return a && b;
}

void chain(int x, int *p)
{
  if (x == 1)
    *p = 10;
  else if (x == 2)
    *p = 20;
  else
    *p = 30;
}

int inloop(int n)
{
  int i;
  for (i = 0; i < 10; i++)
    if (i == n)
      return i ? 5 : 6;
  return 7;
}

int main(void)
{
  int v, w;
  chain(2, &v);
  chain(9, &w);
  if (cond(1, 2, 3) != 2 || cond(0, 2, 3) != 3 || both(1, 1) != 1
      || both(1, 0) != 0 || v != 20 || w != 30)
    return 1;
  return inloop(0) != 6 || inloop(3) != 5 || inloop(20) != 7;
}
