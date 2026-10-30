// expect: duplicate struct member 'x'
/* Member names must differ within a struct. */
struct pt { int x, y; char x; };

int main()
{
  return 0;
}
