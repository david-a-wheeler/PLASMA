// expect: redeclaration of 'RED'
/* An enum constant can't share a name with a global variable. */
int RED;
enum { RED, GREEN };

int main()
{
  return 0;
}
