// expect: :3: error: sizeof can't be applied to type void
void f(void) { }
int main() { return sizeof f(); }
