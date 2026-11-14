// String literals: escapes, joining adjacent literals, pstr(), and a
// literal used as a statement.
int main()
{
  __cmd_puts(pstr("one, " "two"));
  __cmd_putln();
  __cmd_puts(pstr("tab:\t| quote:\" backslash:\\ end"));
  __cmd_putln();
  __cmd_puts(pstr(""));
  __cmd_puts(pstr("x"));
  __cmd_putln();
  "unused literal";
  return 0;
}
