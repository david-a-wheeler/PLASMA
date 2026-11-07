/* The union-half: a union passed by value across a module boundary.
 * The tag's name, not its identity, is what the signature hash carries,
 * so the caller's own union W (tests/link/union/b.c) matches this
 * one. */
union W { int a; char b; };

int f(union W u)
{
  return u.a + u.b;
}
