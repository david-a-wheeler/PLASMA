// When an expression is too long for ccgen's IR buffer (it lowers
// the older half when full, keeping at least the last 64 IR ops), the
// address's code has already been lowered by the time the value's code
// is read, so the two can't be exchanged by moving code (approach.md
// 18.7, A). A store then exchanges the values when the code runs,
// through ccgen's two temporaries (at frame offsets 4 and 6, past main's
// locals), keeping the value's copy when the store's value is used; and
// i + p saves i in one temporary just before p's code, which is still
// held. Each sum below is 100 x's, so its code is 199 IR ops.
// listing-has: ENTER 8,0
// listing-has: SLW 6
int a[4];

void show(int n)
{
  __puti(n);
  __putc(' ');
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
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x;
  show(a[1]);
  show(*pick(2) = x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x);
  show(a[2]);
  show(*((x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x
      + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x - 99) + p));
  __putln();
  return 0;
}
