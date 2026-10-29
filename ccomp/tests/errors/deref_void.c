// expect: :2: error 67: dereferencing a 'void *'
int main() { int x; void *v = &x; return *v; }
