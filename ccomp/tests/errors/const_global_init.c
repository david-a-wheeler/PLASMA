// expect: a const variable's value isn't a constant expression in C (an initializer here must be one)
/* A global's initializer must be a constant expression, and a const
 * variable isn't one in C. */
const int N = 3;
int M = N + 1;

int main()
{
  return M;
}
