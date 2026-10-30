// expect: expected '{' to start a struct initializer, not
/* A global's initializer must be constant: not another struct. */
struct pt { int x, y; };

struct pt a;
struct pt b = a;

int main()
{
  return 0;
}
