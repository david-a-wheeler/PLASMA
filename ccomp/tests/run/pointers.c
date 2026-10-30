/* Pointers: & and *, pointer parameters and return values, arithmetic
 * scaled by the element size, comparisons (< > <= >= are unsigned),
 * stores through pointers, and void *. */
// listing-has: LLA
// listing-has: IDXLW
// listing-has: SB
// listing-has: SW
// listing-has: LB
// listing-has: LW
void show(int n)
{
  puti(n);
  putc(' ');
}

void swap(int *a, int *b)
{
  int t = *a;
  *a = *b;
  *b = t;
}

int len(char *s)
{
  int n = 0;
  while (*s++)
    n++;
  return n;
}

int *pick(int *a, int *b, int first)
{
  return first ? a : b;
}

/* Fill n ints from p with start, start + 1, ... */
void fill(int *p, int n, int start)
{
  while (n-- > 0)
    *p++ = start++;
}

int main()
{
  int x = 1, y = 2, *p = &x, **pp = &p;
  char c = 'A', *cp = &c, *s;
  int *w;
  void *v;

  /* & and * on locals */
  show(*p);
  *p = 10;
  show(x);
  show(*p = 7);
  show(x = *p + 1);
  swap(&x, &y);
  show(x);
  show(y);
  **pp = 5;
  show(x);
  *pp = &y;
  show(*p);
  show(&*p == p);
  show(*pick(&x, &y, 1));
  show(*pick(&x, &y, 0));
  putln();

  /* chars through a pointer */
  *cp = 300;
  show(c);
  show(*cp = 321);
  (*cp)++;
  show(c);
  show(++*cp);
  show((*cp)--);
  show(c);
  *cp += 200;
  show(c);
  show(len("hello"));
  show(len(""));
  putln();

  /* arithmetic on a heap block of 5 ints */
  v = heapalloc(10);
  w = v;
  fill(w, 5, 100);
  show(*w);
  show(*(w + 4));
  show(*(2 + w));
  p = w + 4;
  show(p - w);
  show(w - p);
  show(*(p - 1));
  show(*--p);
  show(*p++);
  show(*p);
  p -= 3;
  show(*p);
  p += 2;
  show(*p);
  y = 1;
  *(w + y) += 10;
  show(*(w + 1));
  show(*(y + w));
  putln();

  /* comparisons */
  s = "hello";
  show(*(s + 1));
  show(s + 5 - s);
  show(w < w + 1);
  show(w + 1 <= w);
  show((char *)0x7FFF < (char *)0x8001);
  show((char *)0xFFFF > (char *)1);
  show(p == w);
  show(p != 0);
  show(!p);
  p = 0;
  show(!p);
  show(p ? 1 : 2);
  show(v == w);
  putln();

  (void)*w;
  putc('!');
  putln();
  return 0;
}
