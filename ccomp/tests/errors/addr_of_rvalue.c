// expect: :2: error: lvalue required as unary '&' operand
int main() { int x; int *p = &(x + 1); return 0; }
