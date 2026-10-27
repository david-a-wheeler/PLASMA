// expect: :2: error: type mismatch in conditional expression
int main() { int x, *p = &x; return x ? p : 1; }
