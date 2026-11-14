// const and volatile (M5 step 6). Reading a const scalar with a
// constant initializer gives its value at compile time, so x < N is
// "LLW x CN 10 ISLT", and N * 2 + local is the constant 25; but &N
// still works, and sizeof N is a constant. A const volatile object is
// read from memory every time, never folded. A pointer may gain const
// on what it points to (char * to const char *); losing const or
// volatile is a warning, as in gcc. Pointers compare, and ?: combines,
// whatever their targets' qualifiers.
// listing-has: CN 10
// listing-has: CB 25




const int N = 10;
const char LETTER = 'A';
static const int ZERO = 0;
int const M = 3;
const volatile int CV = 7;
const char *names[] = {"zero", "one"};

void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

int len(char *s)
{
  int n = 0;
  while (*s++)
    n++;
  return n;
}

int clen(const char *s)
{
  int n = 0;
  while (*s) {
    s++;
    n++;
  }
  return n;
}

int main()
{
  const int local = 5;
  int x = 3, buf[sizeof N];
  const char *p = "hi";
  char *q = p;
  const int *ip = &N;
  char *s;
  show(x < N);
  show(N * 2 + local);
  show(LETTER);
  show(*ip);
  show(clen(p));
  show(len(p));
  p = names[1];
  show(clen(p));
  show(M);
  show(CV);
  show(ZERO);
  show(sizeof buf);
  {
    volatile int v = 1;
    int *vp = &v;
    show(*vp);
  }
  show(q[0]);
  show(p == q);
  p = x ? q : p;
  show(p[1]);
  s = x ? q : names[0];
  show(s[0]);
  __cmd_putln();
  return 0;
}
