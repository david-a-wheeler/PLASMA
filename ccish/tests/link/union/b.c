/* The caller-half: see a.c for the union and the callee. */
union W { int a; char b; };

int f(union W u);

int main(void)
{
  union W u;
  u.a = 0x1234;
  puti(f(u) + u.b);
  putln();
  return 0;
}
