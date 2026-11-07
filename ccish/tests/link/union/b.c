/* The caller-half: see a.c for the union and the callee. */
union W { int a; char b; };

int f(union W u);

int main(void)
{
  union W u;
  u.a = 0x1234;
  __puti(f(u) + u.b);
  __putln();
  return 0;
}
