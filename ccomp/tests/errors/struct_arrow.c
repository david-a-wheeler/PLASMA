// expect: '->' needs a pointer to a struct
/* '->' needs a pointer to a struct, not a struct. */
struct pt { int x, y; };

int main()
{
  struct pt p;
  p->x = 1;
  return 0;
}
