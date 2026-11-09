
// The result of an assignment through a pointer is not an lvalue, even
// while its store waits to be emitted.
int a[4];

int main()
{
  int i = 1;
  (a[i + 1] = 2) = 3;
  return 0;
}
