/* When an expression is too long for stage 2's buffer (64 IR ops), the
 * address's code has already been lowered by the time the value is
 * parsed, so it can't move. A store then swaps after the fact with two
 * temporaries (at frame offsets 4 and 6), and i + p saves p in a
 * temporary, as before rotation existed (approach.md 4(d2)). Each sum
 * below is 40 x's, so its code is 79 IR ops. */
// listing-has: ENTER 8,0
// listing-has: SLW 6
int a[4];

void show(int n)
{
  puti(n);
  putc(' ');
}

int *pick(int i)
{
  return &a[i];
}

int main()
{
  int x = 1, *p = a;
  *pick(1) = x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x;
  show(a[1]);
  show(*pick(2) = x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x);
  show(a[2]);
  show(*((x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x - 39) + p));
  putln();
  return 0;
}
