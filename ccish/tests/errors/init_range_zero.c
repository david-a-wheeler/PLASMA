int f(void)
{
  static int z[2];
  static int *p = z + 3;
  return *p;
}

int main(void)
{
  return f();
}
