// Address constants (approach.md, "C6 design"): the expression parser
// folds a global's or a string's address with constant offsets, in a
// global's initializer (2 + buf, &s.y and &ps[1].x were errors before)
// and in code, as one LA. The data: s at 0, ps at 4, buf at 12, m at
// 20, so buf + 4 is data+16.
// listing-has: ; data+16
struct pt { int x, y; };
struct pt s = {10, 20};
struct pt ps[2] = {1, 2, 3, 4};
char buf[8] = "abcdefg";
int m[2][3] = {1, 2, 3, 4, 5, 6};
char *p1 = 2 + buf;
int *p2 = &s.y;
int *p3 = &ps[1].x;
int *p5 = (int *)((char *)&m[1][0] + 2);
char *p6 = "xyz" + 1;
int *p7 = m[1] + 2;

void show(char *t)
{
  while (*t)
    __cmd_putc(*t++);
  __cmd_putc(' ');
}

int main()
{
  char *q = buf + 4;
  show(p1); __cmd_puti(*p2); __cmd_putc(' '); __cmd_puti(*p3); __cmd_putc(' ');
  __cmd_puti(*p5); __cmd_putc(' '); show(p6); __cmd_puti(*p7); __cmd_putc(' ');
  show(q); __cmd_puti(*(&m[0][0] + 4));
  __cmd_putln();
  return 0;
}
