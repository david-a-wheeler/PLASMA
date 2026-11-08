/* An extern in a block would be hidden by a visible local of the same name. */
int main()
{
  int x;
  {
    extern int x;
  }
  return 0;
}
