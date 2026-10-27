// expect: :3: error: conflicting declaration of 'f'
char f();
int f() { return 0; }
int main() { return f(); }
