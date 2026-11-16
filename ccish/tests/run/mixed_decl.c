// A global declaration may declare a variable and a function in one
// (int x, f(int);), with or without static (ptop.c, parse_globals):
// the function is declared with the declaration's storage, sf shared
// and ssf local, and both are defined and used here, the variables'
// values through them, so a declaration that dropped a name or a
// value shows.
int gv = 3, sf(int);
static int gs = 4, ssf(int);

int sf(int n)
{
  return n + gv;
}

static int ssf(int n)
{
  return n + gs;
}

int main(void)
{
  __cmd_puti(sf(1));
  __cmd_putc(' ');
  __cmd_puti(ssf(1));
  __cmd_putc(' ');
  __cmd_puti(gv + gs);
  __cmd_putln();
  return 0;
}
