// expect: called object is not a function: 'A'
/* An enum constant can't be called. */
enum { A, B };

int main()
{
  return A();
}
