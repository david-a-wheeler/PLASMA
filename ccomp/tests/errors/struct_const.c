// expect: a 'const' object can't be the left operand of assignment
/* A member of a const struct is const. */
struct pt { int x, y; };

const struct pt origin;

int main()
{
  origin.y = 1;
  return 0;
}
