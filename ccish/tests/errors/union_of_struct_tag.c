// Struct and union tags share a name space: a struct tag used as a
// union is a redefinition.
struct s { int a; };
union s u;

int main()
{
  return 0;
}
