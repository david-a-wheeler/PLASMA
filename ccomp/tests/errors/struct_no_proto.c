// expect: passing a struct needs a prototype for 'show'
/* Without a prototype, the caller can't know how the callee takes a
 * struct. */
struct pt { int x, y; };

int main()
{
  struct pt p;
  show(p);
  return 0;
}
