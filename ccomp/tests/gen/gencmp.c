/* gencmp.c - the comparisons as branches: the compare fuses with the
 * test branch into BREQ or BRNE, comparing with 0 is just a test of
 * the value, and a negation feeds the branch with the opposite test.
 * The if-parts end in returns, so no jump follows them. */
int eq(int a, int b)  { if (a == b) return 1; else return 0; }
int ne(int a, int b)  { if (a != b) return 1; else return 0; }
int isz(int a)        { if (a == 0) return 1; else return 0; }
int nz(int a)         { if (a != 0) return 1; else return 0; }
int ngt(int a, int b) { if (!(a > b)) return 1; else return 0; }
int negx(int x)       { if (-x) return 1; else return 0; }
int notx(int x)       { if (!x) return 1; else return 0; }

int main(void)
{
  eq(1, 1);
  ne(1, 1);
  isz(0);
  nz(0);
  ngt(1, 1);
  negx(0);
  notx(0);
  return 0;
}
