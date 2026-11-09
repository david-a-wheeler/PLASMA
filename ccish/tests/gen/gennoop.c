// gennoop.c - a constant 0 under a no-op op (x | 0, x ^ 0, x << 0,
// x >> 0), or a double complement (~~x), leaves the value unchanged, so
// both ops go. A nonzero constant (x | 1) is not a no-op and stays,
// as one ORI.
int or0(int x)    { return x | 0; }
int xor0(int x)   { return x ^ 0; }
int shl0(int x)   { return x << 0; }
int shr0(int x)   { return x >> 0; }
int dblcomp(int x){ return ~~x; }
int or1(int x)    { return x | 1; }

int main(void)
{
  or0(42);
  xor0(42);
  shl0(42);
  shr0(42);
  dblcomp(42);
  or1(42);
  return 0;
}
