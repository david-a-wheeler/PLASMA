
// An automatic variable's address isn't a constant: it is in the
// frame, which exists only while the function runs.
int main()
{
  int x = 1;
  static int *p = &x;
  return *p;
}
