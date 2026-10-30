/* The other half of probe: see a.c. */
int f108802(int n);
extern int f108810;
int f108826(int n);
int f108804 = 4;

/* n less f108804. */
int f108803(int n) { return n - f108804; }

int main()
{
  puti(f108802(1)); putc(' ');
  puti(f108810); putc(' ');
  puti(f108826(3)); putc(' ');
  puti(f108803(10));
  putln();
  return 0;
}
