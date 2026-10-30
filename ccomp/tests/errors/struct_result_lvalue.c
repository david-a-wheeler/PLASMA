// expect: lvalue required as left operand of assignment
/* A member of a returned struct can be read, but not assigned to. */
struct pt { int x, y; };

struct pt origin(void);

int main()
{
  origin().x = 1;
  return 0;
}
