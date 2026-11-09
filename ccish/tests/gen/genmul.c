// genmul.c - multiplying by a constant: 2 and 3 are a dup and an add
// (x + 2x is the word-index op), 0 and 1 vanish (x is still computed,
// for its side effects), and a power of two is a shift.
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
