// expect: :2: error: invalid operands to binary '+'
int main() { int x, *p = &x; p = p + p; return 0; }
