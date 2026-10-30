// expect: a 'const' object can't be the operand of ++ or --
/* ++ and -- on a const local are errors. */
int main()
{
  const int n = 1;
  n++;
  return n;
}
