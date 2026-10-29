// expect: :2: error 45: pointer arithmetic on a 'void *'
int main() { int x; void *v = &x; v = v + 1; return 0; }
