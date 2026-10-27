// expect: :3: error: void value used in an expression
void f() { }
int main() { while (f()) ; return 0; }
