// The caller-half: see a.c for the union and the callee.
union W { int a; char b; };

int f(union W u);

int main(void)
{
  union W u;
  u.a = 0x1234;
  __cmd_puti(f(u) + u.b);
  __cmd_putln();
  return 0;
}
