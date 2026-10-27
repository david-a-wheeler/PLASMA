// expect: :2: error: incompatible pointer types in initialization
int main() { int x; char *p = &x; return 0; }
