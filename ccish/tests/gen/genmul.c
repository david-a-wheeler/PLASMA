// genmul.c - multiplying by a constant: 2 and 3 are a dup and an add
// (x + 2x is the word-index op), 1 vanishes, 0 is the constant 0 (x is
// still computed, for its side effects, when it has any), and a power
// of two is a shift.
int m2(int x)    { return x * 2; }
int m3(int x)    { return x * 3; }
int m0(int x)    { return x * 0; }
int m1(int x)    { return x * 1; }
int m16(int x)   { return x * 16; }
int m1024(int x) { return x * 1024; }

int main(void)
{
  m2(1);
  m3(1);
  m0(1);
  m1(1);
  m16(1);
  m1024(1);
  return 0;
}
