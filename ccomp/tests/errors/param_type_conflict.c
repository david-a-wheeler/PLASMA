// expect: :3: error: conflicting parameter types for 'f'
int f(char c);
int f(int c) { return c; }
int main() { return f(1); }
