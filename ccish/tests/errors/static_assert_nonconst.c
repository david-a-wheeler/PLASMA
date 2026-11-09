int main()
{
  int x = 1;
  _Static_assert(x, "a variable is not a constant");
  return 0;
}
