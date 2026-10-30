// expect: pointer from integer without a cast in initialization
/* A const int holding 0 isn't a null pointer constant in C: only an
 * integer constant expression 0 is. */
int main()
{
  const int ZERO = 0;
  int *p = ZERO;
  return p == 0;
}
