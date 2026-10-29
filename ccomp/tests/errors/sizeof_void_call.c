// expect: :3: error 78: sizeof can't be applied to type void
void f(void) { }
int main() { return sizeof f(); }
