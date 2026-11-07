/* ++ and --, prefix and postfix, with values used and unused. Only a
 * postfix whose old value is used needs a DUP (three here); a
 * statement like i++ compiles to LLW INCR SLW. */
// listing-count: 3 DUP
// listing-has: INCR
// listing-has: DECR
int main()
{
  int i = 5, j;
  i++;
  ++i;
  i--;
  --i;
  __puti(i); __putc(' ');
  j = i++; __puti(j); __puti(i); __putc(' ');
  j = ++i; __puti(j); __puti(i); __putc(' ');
  j = i--; __puti(j); __puti(i); __putc(' ');
  j = --i; __puti(j); __puti(i); __putc(' ');
  __puti(-i++); __puti(i); __putln();
  return 0;
}
