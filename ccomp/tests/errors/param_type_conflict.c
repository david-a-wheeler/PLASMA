// expect: :3: error 31: conflicting parameter types for 'f'
int f(char c);
int f(int c) { return c; }
int main() { return f(1); }
