// gendiv.c - dividing by 1 vanishes, signed or unsigned (x is still
// computed, for its side effects: f is still called), and x % 1 is 0
// (DROP CN 0, then ANDI 0, then just CN 0, as x is a plain load). A
// signed divide by 2 stays a DIV: SHR rounds down, but C's / truncates
// toward zero.
int d1(int x)           { return x / 1; }
int m1(int x)           { return x % 1; }
unsigned u1(unsigned x) { return x / 1u; }
int d2(int x)           { return x / 2; }
int f(void)             { return 7; }
int c1(void)            { return f() / 1; }

int main(void)
{
  d1(1);
  m1(1);
  u1(1);
  d2(1);
  c1();
  return 0;
}
