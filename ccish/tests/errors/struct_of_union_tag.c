// Struct and union tags share a name space: a union tag used as a
// struct is a redefinition.
union u { int a; char c; };
struct u x;

int main()
{
  return 0;
}
