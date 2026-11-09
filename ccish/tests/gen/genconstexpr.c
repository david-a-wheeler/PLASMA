// genconstexpr.c - a constexpr is a named constant: it takes no
// storage, and its uses compile as its value would, so this is the
// code of the same program with the values written in.
constexpr int SIZE = 8;
constexpr unsigned MASK = 0xF000;
char buf[SIZE];

int f(unsigned x)
{
  constexpr int SHIFT = 2;
  return (x & MASK) == 0 ? SIZE << SHIFT : SIZE;
}

int main(void)
{
  return f(5) - 32;
}
