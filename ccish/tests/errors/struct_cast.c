
// C has no casts to struct types.
struct pt { int x, y; };

int main()
{
  struct pt a;
  (struct pt)a.x;
  return 0;
}
