
// A const parameter can't be assigned to.
int f(const int n)
{
  n = 1;
  return n;
}

int main()
{
  return f(2);
}
