// genzerodup.c - DROP CN 0 (discard a value, then push 0) is one ANDI 0,
// and two equal constants in a row are a constant and a DUP. zero keeps
// x (for its side effects) but drops it to 0 in one op; dargs' two equal
// word arguments are pushed once and duplicated (a DUP is one byte where
// a second CW is three); main's final DROP CN 0 also folds to ANDI 0.
int zero(int x){ return x * 0; }
int dargs(int a, int b){ return a; }
int main(void)
{
  zero(100);
  dargs(1000, 1000);
  return 0;
}
