// Reads of a volatile pointer, a volatile index, and a member of a
// volatile struct, each once as written. The globals: buf data+0 to
// +3, vp data+4, vi data+6, vs data+8 (a) and data+10 (b).
//   *vp, and *vp += 1: vp is read once for each (f1, f4), not again
//   for the store, so 2 reads of data+4;
//   buf[vi]: vi read once (f2);
//   vs.b: one load of data+10 (f3), and none of data+8.
// listing-count: 2 ; data+4
// listing-count: 1 ; data+6
// listing-count: 1 ; data+10
// listing-lacks: ; data+8
char buf[4] = {1, 2, 3, 4};
char *volatile vp = buf;
volatile int vi = 2;
struct s { int a; int b; };
volatile struct s vs = {5, 6};

int f1(void)
{
  return *vp;
}

int f2(void)
{
  return buf[vi];
}

int f3(void)
{
  return vs.b;
}

void f4(void)
{
  *vp += 1;
}

int main()
{
  __puti(f1());
  __putc(' ');
  __puti(f2());
  __putc(' ');
  __puti(f3());
  __putc(' ');
  f4();
  __puti(buf[0]);
  __putln();
  return 0;
}
