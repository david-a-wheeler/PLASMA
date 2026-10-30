// expect: redeclaration of 'T'
/* A typedef may be repeated only with the same type. */
typedef int T;
typedef int T;
typedef char T;

int main()
{
  return 0;
}
