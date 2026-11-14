// ++ and --, prefix and postfix, with values used and unused. Only a
// postfix whose old value is used needs a DUP (three here); a
// statement like i++ compiles to LLW INCR SLW.
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
  __cmd_puti(i); __cmd_putc(' ');
  j = i++; __cmd_puti(j); __cmd_puti(i); __cmd_putc(' ');
  j = ++i; __cmd_puti(j); __cmd_puti(i); __cmd_putc(' ');
  j = i--; __cmd_puti(j); __cmd_puti(i); __cmd_putc(' ');
  j = --i; __cmd_puti(j); __cmd_puti(i); __cmd_putc(' ');
  __cmd_puti(-i++); __cmd_puti(i); __cmd_putln();
  return 0;
}
