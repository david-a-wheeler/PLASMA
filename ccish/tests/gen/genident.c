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
// 0 - x is -x: NEG, for an operand with no effect (a sum of two locals
// too); 0 - a call stays a SUB.
int zsub(int x)      { return 0 - x; }
int zsub2(int x, int y) { return 0 - (x + y); }
int zsubcall(void)   { return 0 - (int)u7(); }
// 0 << x, 0 >> x, 0 / x and 0 % x are 0, for an x with no effect; a
// volatile divisor is still read.
int zshl(int x)      { return 0 << x; }
int zshr(int x)      { return 0 >> x; }
int zdiv(int x)      { return 0 / x; }
int zmod(int x)      { return 0 % x; }
int zdivvol(void)    { return 0 / vg; }
// Two constant shifts are one: their counts add, a left shift by 16 or
// more is 0, and a right shift's count stops at 15.
int shlshl(int x)    { return (x << 2) << 3; }
int shl16(int x)     { return (x << 8) << 8; }
int shrshr(int x)    { return (x >> 2) >> 3; }
int shr20(int x)     { return (x >> 10) >> 10; }

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
  zsub(1);
  zsub2(1, 2);
  zsubcall();
  zshl(1);
  zshr(1);
  zdiv(1);
  zmod(1);
  zdivvol();
  shlshl(1);
  shl16(1);
  shrshr(1);
  shr20(1);
  return 0;
}
