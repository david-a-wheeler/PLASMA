
/* C has no == for structs. */
struct pt { int x, y; };

int main()
{
  struct pt a, b;
  return a == b;
}
