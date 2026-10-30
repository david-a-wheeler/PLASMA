/* Local array initializers, compiled as stores into the frame: brace
 * lists with missing elements (zero) and a trailing comma, sizes from
 * the initializer, char arrays from strings (with and without room for
 * the NUL), flat lists for two-dimensional arrays, string rows of a
 * char array of arrays, non-constant elements, arrays of strings, a
 * scalar in braces, and re-initialization each time a block is entered.
 * Two neighboring char constants are one word store ("abc" starts with
 * CW $6261, 'a' and 'b'), and long runs of zeros use memset (roomy and
 * big). */
// listing-has: CW $6261
// listing-count: 2 ; memset (import)
void show(int n)
{
  puti(n);
  putc(' ');
}

/* Print the C string s, then a space. */
void say(char *s)
{
  while (*s)
    putc(*s++);
  putc(' ');
}

int sum(int a[], int n)
{
  int s = 0, i;
  for (i = 0; i < n; i++)
    s += a[i];
  return s;
}

int main()
{
  int t[5] = {1, 2, 3};
  int sq[] = {0, 1, 4, 9,};
  char word[] = "abc";
  char exact[3] = "xyz";
  char roomy[20] = "hi";
  int m[2][3] = {1, 2, 3, 4, 5, 6};
  int flat[][2] = {1, 2, 3};
  char kw[][6] = {"int", "char"};
  int x = 7, y = {8};
  int v[3] = {x, x + y, sum(t, 5)};
  char *names[] = {"zero", "one", "two"};
  int big[30] = {5};
  int i;

  for (i = 0; i < 5; i++)
    show(t[i]);
  show(sizeof sq / sizeof sq[0]);
  show(sq[3]);
  putln();

  say(word);
  putc(exact[0]);
  putc(exact[2]);
  putc(' ');
  say(roomy);
  show(roomy[19]);
  show(sizeof roomy);
  putln();

  show(m[1][2]);
  show(sizeof flat);
  show(flat[1][0]);
  show(flat[1][1]);
  say(kw[0]);
  say(kw[1]);
  show(kw[1][5]);
  putln();

  show(x);
  show(y);
  for (i = 0; i < 3; i++)
    show(v[i]);
  for (i = 0; i < 3; i++)
    say(names[i]);
  show(big[0]);
  show(big[29]);
  show(sum(big, 30));
  putln();

  /* initialized again each time the block is entered */
  for (i = 0; i < 2; i++) {
    int c[3] = {i};
    show(c[0] + c[2]);
    c[2] = 9;
  }
  putln();
  return 0;
}
