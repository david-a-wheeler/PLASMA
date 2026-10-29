// expect: :3: error 82: only one side of '?:' is void
void f() { }
int main() { int x = 1; x ? f() : 1; return 0; }
