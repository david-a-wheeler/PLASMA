
// Two structs with the same members are still different types.
struct a { int x; };
struct b { int x; };

int main()
{
  struct a p;
  struct b q;
  p = q;
  return 0;
}
