// Calls: argument order, prototypes, forward references, a
// declaration without parameters, void functions, nesting and
// parentheses.
int twice(int x);                   // prototype; defined below
int later();                        // the first call fixes the count

void three(int a, int b, int c)
{
  __cmd_puti(a); __cmd_puti(b); __cmd_puti(c); __cmd_putln();
}

int sum3(int a, int b, int c) { return a + b + c; }

void nothing() { return; }

int main()
{
  three(1, 2, 3);
  __cmd_puti(twice(21)); __cmd_putln();
  __cmd_puti(later(4)); __cmd_putln();        // declared with ()
  __cmd_puti(sum3(100, twice(10), sum3(1, 2, 3))); __cmd_putln();
  nothing();
  __cmd_puti(((1 + (2)) + 3)); __cmd_putln();
  return 99;                      // the exit status (calls.status)
}

int twice(int x) { return x + x; }
int later(int n) { return n + 1; }
