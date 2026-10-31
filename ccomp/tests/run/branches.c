/* Conditions compile to plain branches: no NEG or ANDI to make C's 1,
 * and == / != / ! / == 0 become single branch ops. */
// listing-lacks: NEG
// listing-lacks: NOT
// listing-lacks: ANDI
// listing-lacks: ISEQ
// listing-lacks: ISNE
// listing-has: BREQ
// listing-count: 3 BRTRU
// listing-has: ISLT
void show(int n)
{
  puti(n);
  putc(' ');
}

int is_zero(int x)
{
  if (x == 0)
    return 1;
  return 0;
}

int differ(int x, int y)
{
  if (x != y)
    return 1;
  return 0;
}

int is_not(int x)
{
  if (!x)
    return 1;
  return 0;
}

int up_to(int x, int y)
{
  while (x < y)
    x++;
  return x;
}

int main()
{
  show(is_zero(0)); show(is_zero(3)); show(differ(1, 2));
  show(differ(2, 2)); show(is_not(0)); show(is_not(5)); show(up_to(1, 4));
  putln();
  return 0;
}
