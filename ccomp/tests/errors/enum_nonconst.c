
/* An enum value must be known at compile time. */
int x;
enum { A = x };

int main()
{
  return 0;
}
