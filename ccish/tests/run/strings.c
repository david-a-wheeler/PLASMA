// String literals: escapes, joining adjacent literals, pstr(), and a
// literal used as a statement.
int main()
{
  __puts(pstr("one, " "two"));
  __putln();
  __puts(pstr("tab:\t| quote:\" backslash:\\ end"));
  __putln();
  __puts(pstr(""));
  __puts(pstr("x"));
  __putln();
  "unused literal";
  return 0;
}
