
/* Without a prototype, the caller can't know how the callee takes a
 * struct. */
struct pt { int x, y; };
int show();

int main()
{
  struct pt p;
  show(p);
  return 0;
}
