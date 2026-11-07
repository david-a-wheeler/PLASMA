/* Stores to locals that nothing reads before the function returns are
 * dead (the frame goes), and go, with any value they stored that has
 * no effect (peep.c, dead_stores()). Each function's comment says what
 * must stay. */
// What stays: through's 2 SLWs, call's 1; global's SAW; byte's ANDI
// 255 (from its DLB), and the init function's, of main's exit status;
// find's 4 DROPs (its two loop values, at the return in the loop and at
// its exit). one's a * 3 goes with x.
// listing-count: 3  SLW
// listing-count: 1 SAW
// listing-count: 2 ANDI
// listing-count: 4  DROP
// listing-lacks: MUL
int g;
int tab[4] = {1, 2, 3, 4};

void show(int n)
{
  __puti(n);
  __putc(' ');
}

/* x is never read: its store, and a * 3, go. */
int one(int a)
{
  int x;
  x = a * 3;
  return a;
}

/* Two dead stores in a row. */
int two(int a)
{
  int x, y;
  x = a;
  y = a + 1;
  return 7;
}

/* Stored, then read back: what is left is the value. */
int back(int a)
{
  int x;
  x = a + 2;
  return x;
}

/* A pointer reads x: its store stays. */
int through(int a)
{
  int x, *p;
  p = &x;
  x = a;
  return *p;
}

/* A global's store stays. */
int global(int a)
{
  g = a;
  return 0;
}

/* A char: the value returned is still the stored byte. */
int byte(int a)
{
  char c;
  c = a;
  return c;
}

/* A call between: the store stays (the scan stops at a call). */
int __call(int a)
{
  int x;
  x = a;
  show(0);
  return 1;
}

/* A counted loop, then a return: the loop needn't store i. */
int find(int n)
{
  int i;
  for (i = 0; i < 4; i++)
    if (tab[i] == n)
      return i;
  return -1;
}

int main()
{
  show(one(5));
  show(two(5));
  show(back(5));
  show(through(5));
  show(global(5));
  show(g);
  show(byte(300));
  show(__call(5));
  show(find(3));
  show(find(9));
  __putln();
  return 0;
}
