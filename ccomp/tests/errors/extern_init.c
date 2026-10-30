// expect: :2: error 216: 'extern' with an initializer is not supported
extern int x = 1;
int main() { return x; }
