// expect: :2: error: comparison of incompatible pointer types
int main() { int x, *p = &x; char c, *q = &c; return p == q; }
