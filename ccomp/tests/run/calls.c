/* Calls: argument order, prototypes, forward references, implicit
 * declarations, void functions, nesting and parentheses. */
int twice(int x);                   /* prototype; defined below */

void three(int a, int b, int c)
{
  puti(a); puti(b); puti(c); putln();
}

int sum3(int a, int b, int c) { return a + b + c; }

void nothing() { return; }

int main()
{
  three(1, 2, 3);
  puti(twice(21)); putln();
  puti(later(4)); putln();        /* implicit declaration */
  puti(sum3(100, twice(10), sum3(1, 2, 3))); putln();
  nothing();
  puti(((1 + (2)) + 3)); putln();
  return 99;                      /* main's result is discarded */
}

int twice(int x) { return x + x; }
int later(int n) { return n + 1; }
