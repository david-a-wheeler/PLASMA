/* if/else, while, do, for, break and continue. */
// listing-lacks: CB 99
// listing-lacks: CB 98
void show(int n)
{
  __puti(n);
  __putc(' ');
}

int sign(int x)
{
  if (x < 0)
    return -1;
  else if (x == 0)
    return 0;
  return 1;
}

int fact(int n)
{
  int r = 1;
  while (n > 1)
    r *= n--;
  return r;
}

/* The first i >= 1 with i * m >= n. */
int first_multiple(int n, int m)
{
  int i;
  for (i = 1; ; i++)
    if (i * m >= n)
      break;
  return i;
}

int main()
{
  int i, sum;
  show(sign(-5)); show(sign(0)); show(sign(7));
  __putln();
  show(fact(5)); show(fact(7)); show(fact(0));
  __putln();
  /* for with continue: the odd numbers below 10 */
  sum = 0;
  for (i = 0; i < 10; i++) {
    if (i % 2 == 0)
      continue;
    sum += i;
  }
  show(sum);
  /* a do loop runs at least once */
  i = 100;
  do
    i++;
  while (i < 50);
  show(i);
  /* continue in a do loop goes to the condition */
  i = 0;
  sum = 0;
  do {
    i++;
    if (i == 3)
      continue;
    if (i > 6)
      break;
    sum += i;
  } while (i < 10);
  show(sum);
  show(first_multiple(20, 7));
  __putln();
  /* nested loops, a declaration in for, break from the inner loop */
  for (int j = 1; j <= 3; j++) {
    int k;
    for (k = 1; ; k++) {
      if (k > j)
        break;
      __puti(k);
    }
    __putc(' ');
  }
  __putln();
  /* a dangling else belongs to the inner if */
  i = 0;
  if (i == 0)
    if (i == 1)
      show(1);
    else
      show(2);
  /* constant conditions: these bodies are never compiled */
  while (0)
    show(99);
  if (0)
    show(98);
  else
    show(3);
  /* for with no condition and no step */
  i = 0;
  for (;;) {
    if (++i == 4)
      break;
  }
  show(i);
  __putln();
  return 0;
}
