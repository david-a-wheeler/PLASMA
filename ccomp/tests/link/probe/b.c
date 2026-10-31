/* The other half of probe: see a.c. */
int f108188(int n);
extern int f108284;
int f108296(int n);
int f108198 = 4;

/* n less f108198. */
int f108189(int n) { return n - f108198; }

int main()
{
  puti(f108188(1)); putc(' ');
  puti(f108284); putc(' ');
  puti(f108296(3)); putc(' ');
  puti(f108189(10));
  putln();
  return 0;
}
