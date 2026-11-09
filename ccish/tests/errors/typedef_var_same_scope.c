
// A typedef and a variable can't share a name in one scope.
int main()
{
  int n;
  typedef int n;
  return 0;
}
