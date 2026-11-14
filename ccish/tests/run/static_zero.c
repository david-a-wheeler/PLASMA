// Static locals with no initializer (doc/xload.md, 3.3) are in the
// file's zero area, not its data: zero at the start, keeping their
// values between calls, each its own object (two x's in two blocks),
// with an address that works in another static local's initializer
// (p) and with constant offsets, a negative one too (buf[i - 1], whose
// -1 is folded into buf's address). One of 600 bytes, over BSS_MIN, is
// in the zero area too: it never moves to the heap, so it's reached
// with absolute ops, LAW and SAW, never through a pointer word.
// listing-lacks: heap global
void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

int count(void)
{
  static int n;
  return ++n;
}

int sum(int k)
{
  static int t[5];
  int i, s = 0;
  t[k] += k + 1;
  for (i = 0; i < 5; i++)
    s += t[i];
  return s;
}

int last(int i)
{
  static char buf[4];
  static char *p = buf + 2;
  buf[i - 1] = 'a' + i;
  return *p;
}

int blocks(void)
{
  int r = 0;
  {
    static int x;
    x += 1;
    r += x;
  }
  {
    static int x;
    x += 10;
    r += x;
  }
  return r;
}

int big(int v)
{
  static int a[300];
  int i, z = 0;
  for (i = 0; i < 300; i++)
    z += a[i] != 0;
  a[299] = v;
  return z * 1000 + a[299];
}

int main(void)
{
  show(count());
  show(count());
  show(count());
  show(sum(0));
  show(sum(2));
  show(sum(0));
  show(last(3));
  show(last(1));
  show(blocks());
  show(blocks());
  show(big(7));
  show(big(9));
  __cmd_putln();
  return 0;
}
