// gencmp.c - truth runs (approach.md, decision 31): an if whose arms
// return 1 and 0 is the condition's truth, so it compiles to the
// condition's value, and the branch, labels and arms are dropped.
// A comparison or ! is already 0 or 1; a plain value is boolified;
// the arms' order picks the negation; equal arms keep one constant.
// The ternary spelling, whose arms end in a branch to the
// expression's end, leaves the value on the stack for what follows.
// Conditions sharing a label with an earlier branch (short-circuit
// && and || values) keep their branches. A ! directly after a
// comparison is the De Morgan opposite of it, one op shorter, valid
// wherever the value goes (decision 32), and !s not after a
// comparison stay.
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

int general_eq(int a, int b)   { if (a == b) return 7; else return 5; }
int general_ne(int a, int b)   { if (a != b) return 7; else return 5; }
int general_isz(int a)         { if (a == 0) return 7; else return 5; }
int general_nz(int a)          { if (a != 0) return 7; else return 5; }
int general_3isz(int a)        { if (a == 3) return 7; else return 5; }
int general_3nz(int a)         { if (a != 3) return 7; else return 5; }
int general_ngt(int a, int b)  { if (!(a > b)) return 7; else return 5; }
int general_negx(int x)        { if (-x) return 7; else return 5; }
int general_notx(int x)        { if (!x) return 7; else return 5; }
int general_iint(int x)        { if (x) return 7; else return 5; }
int general_tret(int x)        { return x ? 7 : 5; }
int general_tsw(int x)         { return x ? 5 : 7; }

int dfv(int a, int b)          { return !(a > b); }
int dfn(int a, int b)          { return !(!(a > b)); }
int dfl(int a, int b)          { if (!(a < b)) return 1; return 0; }
int dbr(int a, int b)          { if (!(a < b)) return 3; return 4; }
int dfl2(unsigned a, unsigned b) { return !(a > b); }
int dfx(int x)                 { return !x; }
int dfa(int x, int y)          { return !(x + y); }
int dfc(int a, int b)          { return a > b; }

int main(void)
{
  eq(1, 1);
  eq(2, 1);
  ne(1, 1);
  ne(2, 1);
  isz(0);
  isz(5);
  nz(0);
  nz(5);
  ngt(1, 1);
  ngt(5, 1);
  negx(0);
  notx(0);
  iint(3);
  tret(3);
  tsw(3);
  tsame(3);
  tadd(3, 10);
  andv(1, 1);
  andv(7, 3);
  orv(0, 1);
  orv(16, 1);

  general_eq(1, 1);
  general_eq(2, 1);
  general_ne(1, 1);
  general_ne(2, 1);
  general_isz(0);
  general_isz(5);
  general_nz(0);
  general_nz(5);
  general_3isz(0);
  general_3nz(0);
  general_ngt(1, 1);
  general_negx(0);
  general_notx(0);
  general_iint(3);
  general_tret(3);
  general_tsw(3);

  dfv(1, 2);
  dfv(2, 1);
  dfn(1, 2);
  dfl(2, 1);
  dbr(2, 1);
  dfl2(1, 2);
  dfx(3);
  dfa(1, 2);
  dfc(2, 1);
  return 0;
}
