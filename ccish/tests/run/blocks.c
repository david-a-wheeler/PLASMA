/* Empty statements, nested blocks, "return;" in an int function (C89
 * allows it; the value is 0), and a void main. */
int f(int x)
{
  { ; }
  { { __puti(x); } }
  ;;
  return;
}

void g()
{
  __putc('g');
  return;
  __putc('X');
}

void main()
{
  __puti(f(7));
  __putln();
  g();
  __putln();
}
