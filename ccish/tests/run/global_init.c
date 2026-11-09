// Global initializers, all computed at compile time and stored in the
// data segment, so main has no code to set them up (no CB 42, and no
// string addresses in the code: the strings are only reached through
// the data). Covers integer and char constants, sizeof, string
// pointers, char arrays from strings (with and without room for the
// NUL), brace lists with missing elements (zero) and a trailing comma,
// sizes from the initializer, flat lists for two-dimensional arrays,
// address constants (&x, an array's name, &m[1][1], + a constant, a
// cast, a global's own address), arrays of strings, rows of a char
// array from strings, and a scalar in braces.
// listing-lacks: CB 42
// listing-lacks: ; string+
int n = 5;
int neg = -3 * 4;
char c = 'A' + 1;
char wrap = 300;
int size = sizeof(int[10]);
int *np = 0;
char *msg = "hello";
char *empty = "";
int table[5] = {1, 2, 3};
int sq[] = {0, 1, 4, 9,};
char word[] = "abc";
char exact[3] = "xyz";
char roomy[6] = "hi";
int m[2][3] = {1, 2, 3, 4, 5, 6};
int part[2][2] = {7};
int flat[][2] = {1, 2, 3};
int *pn = &n;
int *pt = table + 2;
int *pm = &m[1][1];
int *pend = &sq[4];
char *pw = word;
char *tail = "hello" + 3;
char *names[] = {"zero", "one", "two"};
char kw[][6] = {"int", "char", "void"};
int braced = {42};
char *speaker = (char *)0xC030;
int *self = (int *)&self;
int count;

void show(int n)
{
  __puti(n);
  __putc(' ');
}

// Print the C string s, then a space.
void say(char *s)
{
  while (*s)
    __putc(*s++);
  __putc(' ');
}

int main()
{
  int i;

  show(n);
  show(neg);
  show(c);
  show(wrap);
  show(size);
  show(np == 0);
  __putln();

  say(msg);
  say(empty);
  say(tail);
  say(word);
  say(roomy);
  __putc(exact[0]);
  __putc(exact[2]);
  __putc(' ');
  __putln();

  for (i = 0; i < 5; i++)
    show(table[i]);
  show(sizeof sq / sizeof sq[0]);
  show(sq[3]);
  show(sizeof word);
  show(sizeof exact);
  show(sizeof roomy);
  show(roomy[5]);
  __putln();

  show(m[1][2]);
  show(part[0][0]);
  show(part[1][1]);
  show(sizeof flat);
  show(flat[1][0]);
  show(flat[1][1]);
  __putln();

  show(*pn);
  *pn = 6;
  show(n);
  show(*pt);
  show(*pm);
  show(pend - sq);
  say(pw);
  __putln();

  for (i = 0; i < 3; i++)
    say(names[i]);
  show(sizeof names);
  for (i = 0; i < 3; i++)
    say(kw[i]);
  show(sizeof kw);
  show(braced);
  show(speaker == (char *)0xC030);
  show(self == (int *)&self);
  show(count);
  __putln();
  return 0;
}
