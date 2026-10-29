// expect: :3: error 47: pointer from integer without a cast in argument 1 of 'f'
void f(int *p) { }
int main() { f(1); return 0; }
