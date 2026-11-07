/* The other half of probe: see a.c. */
int f108188(int n);
extern int f108284;
int f108296(int n);
int f108198 = 4;

/* n less f108198. */
int f108189(int n) { return n - f108198; }

int main()
{
  __puti(f108188(1)); __putc(' ');
  __puti(f108284); __putc(' ');
  __puti(f108296(3)); __putc(' ');
  __puti(f108189(10));
  __putln();
  return 0;
}
