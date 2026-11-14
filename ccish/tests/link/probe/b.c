// The other half of probe: see a.c.
int f108018(int n);
extern int f108042;
int f108050(int n);
int f108028 = 4;

// n less f108028.
int f108019(int n) { return n - f108028; }

int main()
{
  __cmd_puti(f108018(1)); __cmd_putc(' ');
  __cmd_puti(f108042); __cmd_putc(' ');
  __cmd_puti(f108050(3)); __cmd_putc(' ');
  __cmd_puti(f108019(10));
  __cmd_putln();
  return 0;
}
