// expect: :3: error 169: parameter count differs from earlier use of 'f'
int f(int a);
int f(int a, int b) { return a + b; }
