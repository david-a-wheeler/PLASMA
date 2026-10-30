// expect: a case label must be an integer constant
/* A case value must be known at compile time. */
int main()
{
  int x = 1, y = 2;
  switch (x) {
  case y: x = 3;
  }
  return x;
}
