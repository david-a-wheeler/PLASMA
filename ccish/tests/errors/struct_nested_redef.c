
/* A struct can't be redefined inside its own member list. */
struct s { int a; struct s { int b; } c; };

int main()
{
  return 0;
}
