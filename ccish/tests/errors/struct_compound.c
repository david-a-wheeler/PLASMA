
// There is no arithmetic on structs.
struct pt { int x, y; };

int main()
{
  struct pt a, b;
  a += b;
  return 0;
}
