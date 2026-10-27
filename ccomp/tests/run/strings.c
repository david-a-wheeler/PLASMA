/* String literals: escapes, joining adjacent literals, pstr(), and a
 * literal used as a statement. */
int main()
{
    puts(pstr("one, " "two"));
    putln();
    puts(pstr("tab:\t| quote:\" backslash:\\ end"));
    putln();
    puts(pstr(""));
    puts(pstr("x"));
    putln();
    "unused literal";
    return 0;
}
