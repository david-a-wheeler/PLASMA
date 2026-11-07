/* Calls: argument order, prototypes, forward references, a
 * declaration without parameters, void functions, nesting and
 * parentheses. */
int twice(int x);                   /* prototype; defined below */
int later();                        /* the first call fixes the count */

void three(int a, int b, int c)
{
  __puti(a); __puti(b); __puti(c); __putln();
}

int sum3(int a, int b, int c) { return a + b + c; }

void nothing() { return; }

int main()
{
  three(1, 2, 3);
  __puti(twice(21)); __putln();
  __puti(later(4)); __putln();        /* declared with () */
  __puti(sum3(100, twice(10), sum3(1, 2, 3))); __putln();
  nothing();
  __puti(((1 + (2)) + 3)); __putln();
  return 99;                      /* the exit status (calls.status) */
}

int twice(int x) { return x + x; }
int later(int n) { return n + 1; }
