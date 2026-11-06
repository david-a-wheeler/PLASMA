/* An extern array's address, plus a constant, in an initializer and in
 * code (approach.md, "C6 design"): relocated by its symbol. */
extern int tab[];
int *pt = tab + 2;
int main()
{
  puti(*pt); putc(32); puti(*(tab + 3)); putln();
  return 0;
}
