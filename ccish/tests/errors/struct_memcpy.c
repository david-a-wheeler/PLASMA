
/* A struct copy calls memcpy, so a memcpy of the program's own must
 * take C's arguments. */
struct big { int v[10]; };

void __memcpy(int n);

int main()
{
  struct big a, b;
  a = b;
  return 0;
}
