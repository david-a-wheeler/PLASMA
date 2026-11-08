/* The other half of probe: see a.c. */
int f108018(int n);
extern int f108042;
int f108050(int n);
int f108028 = 4;

/* n less f108028. */
int f108019(int n) { return n - f108028; }

int main()
{
  __puti(f108018(1)); __putc(' ');
  __puti(f108042); __putc(' ');
  __puti(f108050(3)); __putc(' ');
  __puti(f108019(10));
  __putln();
  return 0;
}
