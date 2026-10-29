// expect: :2: error 41: lvalue required as operand of ++ or --
int main() { int a; a = 1; return (a + 1)++; }
