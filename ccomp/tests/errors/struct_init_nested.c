// expect: nested braces in initializers are not supported yet
/* Nested braces come in M11; a flat list works now. */
struct pt { int x, y; };

struct pt ps[2] = {{1, 2}, {3, 4}};

int main()
{
  return 0;
}
