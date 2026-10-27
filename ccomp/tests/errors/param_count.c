// expect: :3: error: parameter count differs from earlier use of 'f'
int f(int a);
int f(int a, int b) { return a + b; }
