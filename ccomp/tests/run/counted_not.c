/* Loops that must not be counted (approach.md, "Loop design", step 2),
 * though they have a counted loop's shape: the body changes the
 * variable, or the limit, calls, or returns; the variable's address is
 * taken before the loop, or later in an outer loop, before the inner
 * loop runs again. Each must give C's results, and no counted-loop op
 * may appear. */
// listing-lacks: INCBRLE
// listing-lacks: ADDBRLE
// listing-lacks: DECBRGE
// listing-lacks: SUBBRGE
// listing-lacks: BRLT
// listing-lacks: BRGT
void show(int n)
{
  puti(n);
  putc(' ');
}

/* The first i whose square is at least n: returns from its loop. */
int find(int n)
{
  int i;
  for (i = 0; i < 100; i++)
    if (i * i >= n)
      return i;
  return -1;
}

/* Not counted: the body changes the variable, or the limit, or calls;
 * the variable's address is taken. */
void not_counted(void)
{
  int i, n, s, *p;
  s = 0;
  for (i = 0; i < 10; i++) {
    if (i == 2)
      i = 7;
    s++;
  }
  show(s);
  show(i);
  s = 0;
  n = 10;
  for (i = 0; i < n; i++) {
    if (i == 1)
      n = 3;
    s++;
  }
  show(s);
  show(i);
  s = 0;
  for (i = 0; i < 3; i++)
    s += find(i * 10);
  show(s);
  p = &i;
  s = 0;
  for (i = 0; i < 5; i++) {
    *p += 1;
    s++;
  }
  show(s);
  show(i);
  putln();
}

/* The variable's address taken later in an outer loop, before the
 * inner loop runs again. */
void later(void)
{
  int s, t, j, dummy, *p;
  p = &dummy;
  s = 0;
  for (j = 0; j < 2; j++) {
    for (t = 0; t < 5; t++) {
      *p = 4;
      s++;
    }
    p = &t;
  }
  show(s);
  show(t);
  putln();
}

int main()
{
  not_counted();
  later();
  return 0;
}
