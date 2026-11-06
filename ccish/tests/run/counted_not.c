/* Loops that must not be counted (approach.md, "Loop design", step 2),
 * though they have a counted loop's shape: the body changes the
 * variable, or the limit, or calls; the variable's address is
 * taken before the loop, or later in an outer loop, before the inner
 * loop runs again; every != loop; and limits the body could change, or
 * that might differ on each pass (limits()). Each must give C's results, and no counted-loop op
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

/* The first i whose square is at least n (a loop that isn't counted,
 * so that a loop calling it has a call in its body). */
int find(int n)
{
  int i = 0;
  while (i * i < n)
    i++;
  return i;
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

/* != loops: never counted (never made a <), so exact for signed and
 * unsigned alike, across $8000 and through 0. */
void not_equal(void)
{
  int i, s = 0;
  unsigned u;
  for (i = 0; i != 5; i++)
    s++;
  show(s);
  show(i);
  s = 0;
  for (i = 9; i != 3; i--)
    s++;
  show(s);
  show(i);
  s = 0;
  for (u = 65530u; u != 4; u++)
    s++;
  show(s);
  show((int)u);
  s = 0;
  for (u = 32760u; u != 32770u; u++)
    s++;
  show(s);
  putln();
}

int gl = 5;
int arr[3] = {1, 4, 6};

/* Return 3. */
int three(void)
{
  return 3;
}

/* Limits that aren't a constant or a local, which the body could
 * change, or that might differ on each pass. */
void limits(void)
{
  int i, s, n, *p, *q;
  s = 0;
  for (i = 0; i < gl; i++) {    /* the body stores to the global */
    if (i == 1)
      gl = 3;
    s++;
  }
  show(s);
  show(i);
  gl = 5;
  p = &gl;
  s = 0;
  for (i = 0; i < gl; i++) {    /* through a pointer */
    if (i == 0)
      *p = 2;
    s++;
  }
  show(s);
  show(i);
  s = 0;
  for (i = 0; i < arr[1]; i++) {    /* to the element the limit reads */
    if (i == 0)
      arr[1] = 2;
    s++;
  }
  show(s);
  show(i);
  s = 0;
  for (i = 0; i < three(); i++)     /* a call in the limit */
    s++;
  show(s);
  show(i);
  n = 6;
  s = 0;
  for (i = 0; i < n - 1; i++) {     /* the body changes a local it reads */
    n--;
    s++;
  }
  show(s);
  show(i);
  n = 4;
  q = &n;
  s = 0;
  for (i = 0; i < n - 1; i++)       /* that local's address taken */
    s++;
  show(s);
  show(i);
  putln();
}

int main()
{
  not_counted();
  later();
  not_equal();
  limits();
  return 0;
}
