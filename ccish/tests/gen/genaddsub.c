// genaddsub.c - two small constant adds or subtracts merge to one op
// (the front end can't fold them, since the intermediate is not a
// constant): ADDI/SUBI and INCR/DECR combine to their sum; a sum of 0
// drops both, and the result is INCR/DECR for +/-1, ADDI/SUBI for the
// rest. A wide constant (300) or a variable second term is not a small
// additive update, so it does not merge.
int sum(int x)
{
  return x + 5 - 3;
}

int zero(int x)
{
  return x + 5 - 5;
}

int cross(int x)
{
  return x + 1 - 3;
}

int crossback(int x)
{
  return x - 5 + 3;
}

int decrements(int x)
{
  return x - 1 - 1;
}

int novar(int x, int y)
{
  return x + 5 + y;
}

int nowide(int x)
{
  return x + 300 - 5;
}

int main(void)
{
  sum(100);
  zero(100);
  cross(100);
  crossback(100);
  decrements(100);
  novar(100, 4);
  nowide(100);
  return 0;
}
