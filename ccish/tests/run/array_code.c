// The code for array elements (approach.md, "Pending array elements"):
// a[i] = x and p[i] = x need no temporary (so the frame is just the
// variables), a constant index on a local array is a plain local, and
// an unused a[i]++ keeps no copy of the old value (no DUP).
// listing-has: ENTER 30,0
// listing-lacks: DUP
// listing-count: 7 IDXLW
// listing-has: SLW 6
// listing-has: SLW 24
// listing-has: LLB 13
void bump(int m[][3], int j)
{
  m[1][j]++;
}

int main()
{
  int a[4], i, x;
  char b[4];
  int *p = a;
  int m[2][3];

  i = 1;
  x = 5;
  a[i] = x;
  a[3] = 9;
  b[i] = 'Q';
  a[i]++;
  p[i]++;
  p[2] = 4;
  b[i]--;
  m[1][0] = 3;
  bump(m, 0);
  __puti(a[1]);
  __putc(' ');
  __puti(a[3]);
  __putc(' ');
  __puti(a[2]);
  __putc(' ');
  __putc(b[1]);
  __putc(' ');
  __puti(m[1][0]);
  __putln();
  return 0;
}
