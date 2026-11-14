// Empty statements, nested blocks, "return;" in an int function (C89
// allows it; the value is 0), and a void main.
int f(int x)
{
  { ; }
  { { __cmd_puti(x); } }
  ;;
  return;
}

void g()
{
  __cmd_putc('g');
  return;
  __cmd_putc('X');
}

void main()
{
  __cmd_puti(f(7));
  __cmd_putln();
  g();
  __cmd_putln();
}
