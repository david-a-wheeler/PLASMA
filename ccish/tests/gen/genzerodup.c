// genzerodup.c - DROP CN 0 (discard a value, then push 0) is one ANDI 0,
// and two equal constants in a row are a constant and a DUP. zero's
// x * 0 is just CN 0, since x, a plain load, has no effect to keep;
// dargs' two equal word arguments are pushed once and duplicated (a DUP
// is one byte where a second CW is three); main's final DROP CN 0 keeps
// the call, and folds to ANDI 0.
int zero(int x){ return x * 0; }
int dargs(int a, int b){ return a; }
int main(void)
{
  zero(100);
  dargs(1000, 1000);
  return 0;
}
