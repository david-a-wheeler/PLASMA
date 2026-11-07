/* Stores through computed addresses need no temporary (approach.md
 * 4(d2), M5 step 4). The address's code is emitted first, as C reads
 * it, then the value's, and stage 2 exchanges the two blocks before
 * lowering them: *d++ = *s++ is "s++ LB d++ SB". If the store's value
 * is used, a DUP after the value keeps it below the address. So copy()
 * has only its parameters in its frame, and main only its locals.
 * Also: nested rotated stores, a held constant before a rotated
 * lvalue, an int stored into a char (the value is its low byte), i + p
 * as "p i IDXW", and *gp += bump(), where bump() moves gp: the call
 * goes first, so the load and the store use the same element. */
// listing-has: ENTER 4,2
// listing-has: ENTER 10,0
// listing-has: IDXW
int g[3];
int *gp;
char buf[8];
int a[6];

void show(int n)
{
  __puti(n);
  __putc(' ');
}

void say(char *s)
{
  while (*s)
    __putc(*s++);
  __putc(' ');
}

void copy(char *d, char *s)
{
  while ((*d++ = *s++) != 0)
    ;
}

int *pick(int i)
{
  return &a[i];
}

int bump(void)
{
  gp++;
  return 10;
}

int main()
{
  int i = 1, j = 2, x, *p;
  char *cp;
  copy(buf, "hello");
  say(buf);
  a[i + 1] = 7;
  show(a[2]);
  *pick(3) = 8;
  show(a[3]);
  x = *pick(4) = 9;
  show(x);
  show(a[4]);
  cp = buf;
  show(*(cp + 1) = 300);
  show(buf[1]);
  a[i + (a[j + 3] = 2)] = 5;
  show(a[5]);
  show(a[3]);
  x = 5 - (a[i + 1] = 2);
  show(x);
  show(a[2]);
  show(10 + (*pick(0) = 4));
  show(a[0]);
  p = a;
  show(*(j + p));
  if ((*pick(5) = 0) == 0)
    show(1);
  gp = g;
  g[0] = 1;
  g[1] = 2;
  g[2] = 3;
  *gp += bump();
  show(g[0]);
  show(g[1]);
  show(g[2]);
  __putln();
  return 0;
}
