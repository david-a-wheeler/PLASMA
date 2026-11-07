/* sizeof: of type names (with '*'s and array sizes), of expressions,
 * which are not run, of arrays (their full size), array parameters (a
 * pointer) and string literals (a char array), and in constant
 * expressions such as an array size: b has 6 elements, so the frame is
 * 10 + 2 + 1 + 2 + 24 + 12 = 51 bytes. f() is never called. */
// listing-has: ENTER 51,0
// listing-lacks: ; f
int g[10];
char gc[7];
int count;

int f(void)
{
  count++;
  return 1;
}

void show(int n)
{
  __puti(n);
  __putc(' ');
}

int param(int a[], char *s)
{
  return sizeof a + sizeof s + sizeof a[0] + sizeof *s;
}

int main()
{
  int a[5], i;
  char c;
  char *p;
  int m[3][4];
  int b[sizeof(int) * 3];

  show(sizeof(int));
  show(sizeof(char));
  show(sizeof(char *));
  show(sizeof(int **));
  show(sizeof(char[10]));
  show(sizeof(int[3][4]));
  show(sizeof(void *));
  show(sizeof(char *[4]));
  __putln();

  show(sizeof a);
  show(sizeof(a));
  show(sizeof a[0]);
  show(sizeof (a)[1]);
  show(sizeof g);
  show(sizeof gc);
  show(sizeof m);
  show(sizeof m[1]);
  show(sizeof m[1][2]);
  show(sizeof b / sizeof b[0]);
  __putln();

  p = gc;
  show(sizeof c);
  show(sizeof p);
  show(sizeof *p);
  show(sizeof &c);
  show(sizeof c * 3);
  show(sizeof(c + c));
  show(sizeof "hello");
  show(sizeof("hi"));
  show(sizeof "hi"[0]);
  show(sizeof "");
  show(sizeof sizeof c);
  show(param(a, p));
  __putln();

  /* the operand isn't run */
  i = 5;
  show(sizeof i++);
  show(i);
  show(sizeof f());
  show(count);
  show(sizeof (f() + i++));
  show(count);
  show(i);
  __putln();
  return 0;
}
