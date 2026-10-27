// expect: :2: error: integer from pointer without a cast in assignment
int main() { int x, *p = &x; x = p; return 0; }
