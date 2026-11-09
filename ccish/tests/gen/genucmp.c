/* genucmp.c - unsigned and pointer < > <= >= call CMDSYS's native
 * ISULT and the rest (lower.c; OBJECT.md, helper numbers 3-6): 3 bytes
 * and about 123 cycles, against 9 and 218 for the sign-bit flips ccgen
 * once wrote (5 and 131 with a constant), so a constant on either side
 * calls too. A comparison with 0 or 1 that tests for zero is that test
 * (irin.c, u_const): x <u 1 and x <=u 0 are x == 0 (NOT), and x >u 0
 * and x >=u 1 are x != 0, one branch in an if. */
unsigned lt(unsigned a, unsigned b) { return a < b; }
unsigned ge(unsigned a, unsigned b) { return a >= b; }
unsigned gt(unsigned a, unsigned b) { return a > b; }
unsigned le(unsigned a, unsigned b) { return a <= b; }
int below(char *p, char *q)  { return p < q; }
int big(unsigned a)          { return a > 40000u; }
int small(unsigned a)        { return 40000u > a; }
int lt1(unsigned a)          { return a < 1; }
int le0(unsigned a)          { return a <= 0; }
int gt0(unsigned a)          { if (a > 0) return 5; return 6; }
int ge1(unsigned a)          { return a >= 1; }

int main(void)
{
  char s[2];
  if (lt(1, 50000u) != 1 || lt(50000u, 1) != 0 || ge(50000u, 1) != 1
      || gt(1, 50000u) != 0 || le(50000u, 50000u) != 1
      || below(s, s + 1) != 1 || below(s + 1, s) != 0)
    return 1;
  if (big(50000u) != 1 || big(30000) != 0 || small(30000) != 1
      || small(50000u) != 0)
    return 2;
  return lt1(0) != 1 || lt1(9) != 0 || le0(0) != 1 || le0(60000u) != 0
         || gt0(0) != 6 || gt0(60000u) != 5 || ge1(0) != 0 || ge1(2) != 1;
}
