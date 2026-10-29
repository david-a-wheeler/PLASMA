// expect: :2: error 41: lvalue required as unary '&' operand
int main() { int x; int *p = &(x + 1); return 0; }
