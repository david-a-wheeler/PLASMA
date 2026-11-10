// genident.c - algebraic identities the peephole stage rewrites
// (peep.c's table): each function is one identity, and the dividend
// or operand is still computed for its side effects where it stays.
// !!x: the first !'s ANDI 1 goes, since NOT tests only zero against
// non-zero; the second keeps its ANDI 1, which makes C's 1.
int notnot(int x)    { return !!x; }

int main(void)
{
  notnot(1);
  return 0;
}
