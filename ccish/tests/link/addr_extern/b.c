// An extern array's address, plus a constant, in an initializer and in
// code (approach.md, "C6 design"): relocated by its symbol.
extern int tab[];
int *pt = tab + 2;
int main()
{
  __puti(*pt); __putc(32); __puti(*(tab + 3)); __putln();
  return 0;
}
