
// The result of an assignment isn't an lvalue, for a struct too.
struct pt { int x, y; };

int main()
{
  struct pt a, b, c;
  (a = b) = c;
  return 0;
}
