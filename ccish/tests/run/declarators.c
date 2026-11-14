// Declarations in C's full syntax (approach.md, "Function pointer
// design", step 1): declarators in parentheses, pointers to arrays,
// pointers to functions and arrays of them, typedefs of function types,
// a function declared through one, a parameter of function type (a
// pointer, C11 6.7.6.3), a variable and a function in one declaration,
// abstract declarators in sizeof and a cast, and block-scope function
// and extern declarations. The externs are among locals, so the locals
// move up a slot (add_global()); u and w show they're intact, also after
// the inner block. Calls through pointers come with step 2.
typedef int fn_t(int);
typedef int (*fp_t)(int);
int (*gp)(int);
int (*table[3])(int);
int a[4] = {10, 20, 30, 40};
int (*pa)[4] = &a;
struct ops {                    // FILEIO's shape: a table of functions
  int (*one)(int);
  char *(*two)(char *, int (*)(int));
  int n;
};
int x, f(int);
int (y);
fn_t g;
int g(int n) { return n + 1; }
int f(int n) { return n * 2; }
int apply(int h(int), int n) { return n; }
int main(void)
{
  int u = 5;
  extern int z;
  int w = 6;
  int k(int);
  fp_t p = 0;
  int (*lp)[4] = &a;
  int *(*r)[2];
  x = 100;
  __cmd_puti(u); __cmd_putc(' '); __cmd_puti(z); __cmd_putc(' '); __cmd_puti(w); __cmd_putln();
  {
    int t = 1;
    extern int x;
    __cmd_puti(t + x); __cmd_putln();
  }
  __cmd_puti(u); __cmd_putc(' '); __cmd_puti(w); __cmd_putln();
  __cmd_puti(sizeof(gp)); __cmd_putc(' '); __cmd_puti(sizeof(table)); __cmd_putc(' ');
  __cmd_puti(sizeof(int (*)[4])); __cmd_putc(' '); __cmd_puti(sizeof(*pa)); __cmd_putc(' ');
  __cmd_puti(sizeof(*r)); __cmd_putc(' '); __cmd_puti(sizeof(fp_t)); __cmd_putc(' ');
  __cmd_puti(sizeof(struct ops)); __cmd_putln();
  __cmd_puti((*pa)[2]); __cmd_putc(' '); __cmd_puti(pa[0][3]); __cmd_putc(' '); __cmd_puti((*lp)[1]); __cmd_putln();
  __cmd_puti(p == 0); __cmd_putc(' '); __cmd_puti(gp == 0); __cmd_putc(' '); __cmd_puti(table[1] == 0); __cmd_putln();
  p = (int (*)(int))0;
  __cmd_puti(p == 0); __cmd_putc(' '); __cmd_puti(k(5)); __cmd_putc(' '); __cmd_puti(f(3)); __cmd_putc(' ');
  __cmd_puti(g(3)); __cmd_putc(' '); __cmd_puti(apply(0, 9)); __cmd_putc(' '); __cmd_puti(y); __cmd_putln();
  return 0;
}
int z = 7;
int k(int n) { return n - 1; }
