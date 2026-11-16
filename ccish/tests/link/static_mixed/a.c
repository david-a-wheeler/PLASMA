// The function in a mixed global declaration (a variable and a
// function in one, int x, f(int);) takes the declaration's storage:
// sf's is static, so it is local to this object; sf2's is not, so it
// is shared, and b.c's use of it links.
static int gv = 3, sf(int);
int gv2 = 4, sf2(int);

int sf(int n)
{
  return n + gv;
}

int sf2(int n)
{
  return n + gv2;
}

int main(void)
{
  return sf(1) + sf2(1);
}
