
/* A global's initializer must be a constant expression, and a const
 * variable isn't one in C. */
const int N = 3;
int M = N + 1;

int main()
{
  return M;
}
