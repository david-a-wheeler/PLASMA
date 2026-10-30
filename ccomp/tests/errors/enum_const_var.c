// expect: a const variable's value isn't a constant expression in C (an enum value must be one)
/* A const variable isn't a constant expression in C. */
const int N = 3;
enum { A = N };

int main()
{
  return 0;
}
