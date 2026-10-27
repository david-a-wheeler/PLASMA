// expect: :3: error: only one side of '?:' is void
void f() { }
int main() { int x = 1; x ? f() : 1; return 0; }
