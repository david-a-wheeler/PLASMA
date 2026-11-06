
/* A static local's initializer is applied when the module loads, so it
 * must be constant, as in C: not an automatic variable's value. */
int main()
{
  int x = 1;
  static int y = x;
  return y;
}
