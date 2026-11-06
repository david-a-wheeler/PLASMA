/* The layout of loops (approach.md, "Loop design"): each test is at
 * the bottom, a true-sense branch back to the body, entered by one
 * jump; an endless loop has no entry jump, just the jump back; and no
 * loop's code jumps around its step. sum's loop is counted (step 2):
 * its init folded into the count, entered at its BRLT. The BRFLS is
 * first_over's if. */
// listing-count: 3 BRNCH
// listing-count: 1 BRTRU
// listing-count: 1 BRFLS
// listing-count: 1 BRLT
int sum(int n)
{
  int i, s = 0;
  for (i = 0; i < n; i++)
    s += i;
  return s;
}

int halve(int n)
{
  int k = 0;
  while (n > 1) {
    n = n / 2;
    k++;
  }
  return k;
}

int first_over(int n)
{
  int i = 0;
  for (;;)
    if (++i * i > n)
      return i;
}

int main()
{
  puti(sum(5));
  putc(' ');
  puti(halve(40));
  putc(' ');
  puti(first_over(50));
  putln();
  return 0;
}
