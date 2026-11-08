/* Function addresses in data, across objects (approach.md, "Function
 * pointer design", step 3): a.c's table names b.c's b_triple, which
 * nothing else in a.c uses, so only the data links it, and a.c's own
 * static inc; b.c's b_neg holds the address of b.c's static neg. */
int b_triple(int x);                /* b.c: only named in the table */
extern int (*b_neg)(int);           /* b.c */
static int inc(int x) { return x + 1; }
int (*tbl[2])(int) = {b_triple, inc};

int main()
{
  __puti(tbl[0](5)); __putc(' ');
  __puti(tbl[1](5)); __putc(' ');
  __puti(b_neg(5)); __putc(' ');
  __puti(b_neg(tbl[0](1)));
  __putln();
  return 0;
}
