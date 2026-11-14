// ccpp's line joins (pplex.c, splice()): a line that ends in a
// backslash goes on in the next, as the lexer reads it, so the limit of
// 255 characters is each physical line's, and a macro whose indented
// lines total more fits. Inside a string or character literal, nothing
// changes: the blanks stay, and a backslash the join leaves escapes the
// next line's first character. Comments around the joins, with quotes
// in them, a block comment over lines, and a // comment that a
// backslash continues, are as C says.
#define SUM6(a, b, c, d, e, f) \
                                                            ((a) \
                                                            + (b) \
                                                            + (c) \
                                                            + (d) \
                                                            + (e) \
                                                            + (f))

int val\
ue = 5;
char s1[] = "ab   \
   cd";
char c1 = '\
n';
char c2 = '\\
n';
char s5[] = "a\\
"   \
   b";
/* the quote here is in a comment that ends on the next line, which
   says " */ char s6[] = "c   \
   d";
/* a comment with a quote "
   and more */ char s3[] = "ef   \
   gh";

int main()
{
  int dc = 7; // it's \
  dc = 8;
  char s4[] = "ij   \
   kl";
  __cmd_puti(SUM6(1, 2, 3, 4, 5, 6)); __cmd_putc(' ');
  __cmd_puti(value); __cmd_putc(' ');
  __cmd_puti(sizeof s1 - 1); __cmd_putc(' ');
  __cmd_puti(c1); __cmd_putc(' ');
  __cmd_puti(c2); __cmd_putc(' ');
  __cmd_puti(sizeof s3 - 1); __cmd_putc(' ');
  __cmd_puti(dc); __cmd_putc(' ');
  __cmd_puti(sizeof s4 - 1); __cmd_putc(' ');
  __cmd_puti(sizeof s5 - 1); __cmd_putc(' ');
  __cmd_puti(sizeof s6 - 1); __cmd_putln();
  return 0;
}
