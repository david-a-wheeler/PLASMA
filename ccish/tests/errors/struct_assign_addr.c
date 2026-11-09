
// The result of a struct assignment has no address.
struct pt { int x, y; };

int main()
{
  struct pt a, b, *p;
  p = &(a = b);
  return 0;
}
