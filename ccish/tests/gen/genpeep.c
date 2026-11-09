// genpeep.c - small peephole rules (peep.c, the list at its top):
// -(-x) is x (NEG NEG goes); x == 0 is NOT (CN 0 ISEQ), and as a test
// one branch (NOT BRFLS is BRTRU); x << 1 is x DUP ADD, as x * 2 is
// (genmul.c); and an address plus a constant is one address
// (LLA n+c).
int g;

int negneg(int x)  { return -(-x); }
int iszero(int x)  { return x == 0; }
int tzero(int x)   { if (x == 0) g = 1; return g; }
int shl1(int x)    { return x << 1; }

int at(char *p) { return *p; }
int addr(void)
{
  char b[4];
  b[0] = 1; b[1] = 2; b[2] = 3; b[3] = 4;
  return at(b + 2);
}

int main(void)
{
  g = 0;
  if (negneg(5) != 5 || iszero(0) != 1 || iszero(3) != 0 || tzero(0) != 1
      || shl1(7) != 14)
    return 1;
  return addr() != 3;
}
