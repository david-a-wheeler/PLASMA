// expect: :2: error: dereferencing a 'void *'
int main() { int x; void *v = &x; return *v; }
