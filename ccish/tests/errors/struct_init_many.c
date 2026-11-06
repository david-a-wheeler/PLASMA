
/* A struct's initializer can't have more values than it has
 * scalars. */
struct pt { int x, y; };

struct pt p = {1, 2, 3};

int main()
{
  return 0;
}
