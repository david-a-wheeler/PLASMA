// Function addresses across objects (approach.md, "Function pointer
// design"): a.c passes b.c's b_double, which it never calls itself, so
// only its address links it, and its own static inc to b.c, which
// calls them through pointers; b.c returns pointers to its functions,
// one static, which a.c calls and compares.
int (*b_pick(int k))(int);          // b.c
int b_apply(int (*f)(int), int x);  // b.c
int b_double(int x);                // b.c: only its address is used
static int inc(int x) { return x + 1; }

int main()
{
  int (*f)(int);
  __cmd_puti(b_apply(inc, 10)); __cmd_putc(' ');
  __cmd_puti(b_apply(b_double, 10)); __cmd_putc(' ');
  f = b_pick(1);
  __cmd_puti(f(10)); __cmd_putc(' ');
  __cmd_puti(b_pick(0) == b_double); __cmd_putc(' ');
  __cmd_puti(b_pick(0) == f);
  __cmd_putln();
  return 0;
}
