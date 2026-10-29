// expect: :3: error 38: void value used in an expression
void f() { }
int main() { while (f()) ; return 0; }
