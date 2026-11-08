/* Nested braces mixed with brace elision, and braces around scalars
 * (C11 6.7.9p11, p20), which gcc accepts with warnings, so this isn't
 * a cross test (tests/xrun/xinitnest.c is). When the next item is '{',
 * it starts the list of the subobject about to be initialized, at
 * whatever depth elision has reached: in m2's {1, {2}, 3}, that is
 * the scalar m2[0][1]. */
struct pt { int x, y; };
struct seg { struct pt a, b; };

int m[2][2] = {{1, 2}, {3, 4}};         /* formerly an error test */
struct pt ps[2] = {{1, 2}, {3, 4}};     /* likewise */
int m2[2][2] = {1, {2}, 3};
struct pt mix[3] = {{1, 2}, 3, 4, {5}};
struct seg sg = {1, 2, {3}};
int a[3] = {{7}, 8};
int x = {9};
char cs[2][3] = {'a', {'b'}, 'c', {'d', 'e'}};

/* Print n ints from p. */
void show(int *p, int n)
{
  int i;
  for (i = 0; i < n; i++) {
    __puti(p[i]);
    __putc(' ');
  }
  __putln();
}

int main()
{
  int lm[2][2] = {1, {2}, 3};
  struct pt lmix[3] = {{1, 2}, 3, 4, {5}};
  int i;
  show(&m[0][0], 4);
  show(&ps[0].x, 4);
  show(&m2[0][0], 4);
  show(&mix[0].x, 6);
  show(&sg.a.x, 4);
  show(a, 3);
  show(&x, 1);
  for (i = 0; i < 6; i++)
    __putc(cs[i / 3][i % 3] ? cs[i / 3][i % 3] : '.');
  __putln();
  show(&lm[0][0], 4);
  show(&lmix[0].x, 6);
  return 0;
}
