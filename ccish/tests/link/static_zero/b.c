int bump_b(void)
{
  static int n;
  n += 10;
  return n;
}

int fill_b(int c)
{
  static char buf[8];
  buf[0] = (char)c;
  return buf[0] + buf[7];
}
