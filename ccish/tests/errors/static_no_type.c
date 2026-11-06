
/* C89's implicit int (static x;) is not supported: a type is required. */
int main()
{
  static x;
  return 0;
}
