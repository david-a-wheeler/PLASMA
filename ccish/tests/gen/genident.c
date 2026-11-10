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

int main(void)
{
  notnot(1);
  ult0(1);
  uge0(1);
  ucall();
  return 0;
}
