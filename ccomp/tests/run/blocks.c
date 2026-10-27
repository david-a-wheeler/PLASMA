/* Empty statements, nested blocks, "return;" in an int function (C89
 * allows it; the value is 0), and a void main. */
int f(int x)
{
    { ; }
    { { puti(x); } }
    ;;
    return;
}

void g()
{
    putc('g');
    return;
    putc('X');
}

void main()
{
    puti(f(7));
    putln();
    g();
    putln();
}
