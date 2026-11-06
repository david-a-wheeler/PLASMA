
/* A const variable isn't a constant expression in C. */
const int N = 3;
enum { A = N };

int main()
{
  return 0;
}
