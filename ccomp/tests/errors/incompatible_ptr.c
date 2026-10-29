// expect: :2: error 48: incompatible pointer types in initialization
int main() { int x; char *p = &x; return 0; }
