// genstaticassert.c - a true _Static_assert emits nothing: at file
// scope, in a block, and between a block's declarations, its code is
// that of the same program without the assertions, and its message
// strings take no data.
_Static_assert(2 + 3 == 5, "five");
_Static_assert(sizeof(int) == 2, "an int is 16 bits");
enum { RED, GREEN, BLUE };
_Static_assert((GREEN | BLUE) == 3, "an enum constant");

int f(int x)
{
  int y = x + 1;
  _Static_assert(1, "between declarations");
  int z = y * 2;
  _Static_assert('A' == 65, "a character constant");
  return z;
}

int main(void)
{
  return f(20) - 42;
}
