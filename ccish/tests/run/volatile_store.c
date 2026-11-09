// A store to a volatile local just before the function returns is
// kept; the plain twin's goes (peep.c, dead stores): 3 SLWs, vstore's
// t = 5 and main's a = 0 and b = 0 (whose addresses are taken). And a
// store through a pointer to volatile, whose value is used, isn't read
// back: a DUP keeps its value (vset), where a plain one reads the
// object again (pset's LW after its SW).
// listing-count: 3 SLW
// listing-count: 2 SW
// listing-count: 1  LW
// listing-count: 1 DUP
void vstore(void)
{
  volatile int t;
  t = 5;
}

void pstore(void)
{
  int u;
  u = 5;
}

int vset(volatile int *p)
{
  return *p = 3;
}

int pset(int *p)
{
  return *p = 4;
}

int main()
{
  int a = 0, b = 0;
  vstore();
  pstore();
  __puti(vset(&a));
  __putc(' ');
  __puti(pset(&b));
  __putc(' ');
  __puti(a + b);
  __putln();
  return 0;
}
