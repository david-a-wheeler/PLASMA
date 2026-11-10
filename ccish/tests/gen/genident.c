// genident.c - algebraic identities the peephole stage rewrites
// (peep.c's table): each function is one identity, and the dividend
// or operand is still computed for its side effects where it stays.
// !!x: the first !'s ANDI 1 goes, since NOT tests only zero against
// non-zero; the second keeps its ANDI 1, which makes C's 1.
int notnot(int x)    { return !!x; }
// Unsigned compares with 0: x < 0u is always 0 and x >= 0u always 1,
// with x dropped (a plain load goes; a call stays).
int ult0(unsigned x) { return x < 0u; }
int uge0(unsigned x) { return x >= 0u; }
unsigned u7(void)    { return 7; }
int ucall(void)      { return u7() >= 0u; }
// x op -1: * and / are NEG, ^ is COMP, & is x, % is 0 and | is -1
// (with x gone, since a plain load has no effect; a call stays).
int mulm1(int x)     { return x * -1; }
int divm1(int x)     { return x / -1; }
int xorm1(int x)     { return x ^ -1; }
int andm1(int x)     { return x & -1; }
int modm1(int x)     { return x % -1; }
int orm1(int x)      { return x | -1; }
int orcall(void)     { return (int)u7() | -1; }
volatile int vg;
int orvol(void)      { return vg | -1; }    // vg is still read
// x with itself, once x's second load is a DUP: x - x and x ^ x are 0,
// x & x and x | x are x. A volatile's two loads are two reads, and stay.
int subself(int x)   { return x - x; }
int xorself(int x)   { return x ^ x; }
int andself(int x)   { return x & x; }
int orself(int x)    { return x | x; }
int subvol(void)     { return vg - vg; }

int main(void)
{
  notnot(1);
  ult0(1);
  uge0(1);
  ucall();
  mulm1(1);
  divm1(1);
  xorm1(1);
  andm1(1);
  modm1(1);
  orm1(1);
  orcall();
  orvol();
  subself(1);
  xorself(1);
  andself(1);
  orself(1);
  subvol();
  return 0;
}
