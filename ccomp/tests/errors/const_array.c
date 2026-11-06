
/* The elements of a const array can't be assigned to. */
int main()
{
  const int t[2] = {1, 2};
  int i = 1;
  t[i] = 3;
  return 0;
}
