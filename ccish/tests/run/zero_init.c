// Globals and static locals whose initializers are all zeros (= 0,
// = {0}, = "", a null pointer) go in the zero-filled space, as if they
// had none (doc/xload.md, 3.3; C11 6.7.9p10 makes them the same): each
// is zero at the start and can be changed. A large one (600 bytes) may
// move to the heap, as an uninitialized one may. One with a nonzero
// byte (one[], with a 1 after a 0) stays in the data, its zero too.
// The gen test genzeroinit.c shows where each goes.
void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

int z = 0;
int za[4] = {0};
char s[3] = "";
int *np = 0;
struct pt { int x; char c; } st = {0, 0};
int big[300] = {0};
int one[2] = {0, 1};

int counter(void)
{
  static int n = 0;
  static char buf[4] = {0, 0};
  buf[3] = (char)(buf[3] + 2);
  return ++n * 100 + buf[3];
}

int main(void)
{
  show(z + za[3] + s[2] + (np == 0) + st.x + st.c + big[299]);
  show(one[0] * 10 + one[1]);
  z = 5;
  za[3] = 6;
  s[1] = 'A';
  st.c = 7;
  big[299] = 8;
  one[0] = 9;
  show(z + za[3] + s[1] + st.c + big[299]);
  show(one[0] * 10 + one[1]);
  show(counter());
  show(counter());
  __cmd_putln();
  return 0;
}
