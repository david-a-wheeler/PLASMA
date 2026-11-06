
/* A static local and an automatic one can't share a name in the same
 * block. */
int main()
{
  int x = 1;
  static int x;
  return x;
}
