// expect: lvalue required as left operand of assignment
/* An enum constant is a value, not a variable. */
enum { A, B };

int main()
{
  A = 2;
  return 0;
}
