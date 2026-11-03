/* gencmp.c - truth runs (approach.md, decision 31): an if whose arms
 * return 1 and 0 is the condition's truth, so it compiles to the
 * condition's value, and the branch, labels and arms are dropped.
 * A comparison or ! is already 0 or 1; a plain value is boolified;
 * the arms' order picks the negation; equal arms keep one constant.
 * The ternary spelling, whose arms end in a branch to the
 * expression's end, leaves the value on the stack for what follows.
 * Conditions sharing a label with an earlier branch (short-circuit
 * && and || values) keep their branches. */
int eq(int a, int b)   { if (a == b) return 1; else return 0; }
int ne(int a, int b)   { if (a != b) return 1; else return 0; }
int isz(int a)         { if (a == 0) return 1; else return 0; }
int nz(int a)          { if (a != 0) return 1; else return 0; }
int ngt(int a, int b)  { if (!(a > b)) return 1; else return 0; }
int negx(int x)        { if (-x) return 1; else return 0; }
int notx(int x)        { if (!x) return 1; else return 0; }
int iint(int x)        { if (x) return 1; else return 0; }
int tret(int x)        { return x ? 1 : 0; }
int tsw(int x)         { return x ? 0 : 1; }
int tsame(int x)       { return x ? 5 : 5; }
int tadd(int x, int y) { y += x ? 1 : 0; return y; }
int andv(int a, int b) { return a && b; }
int orv(int a, int b)  { return a || b; }

int main(void)
{
  eq(1, 1);
  ne(1, 1);
  isz(0);
  nz(0);
  ngt(1, 1);
  negx(0);
  notx(0);
  iint(3);
  tret(3);
  tsw(3);
  tsame(3);
  tadd(3, 10);
  andv(1, 1);
  orv(0, 1);
  return 0;
}
