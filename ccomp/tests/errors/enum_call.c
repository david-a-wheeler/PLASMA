
/* An enum constant can't be called. */
enum { A, B };

int main()
{
  return A();
}
