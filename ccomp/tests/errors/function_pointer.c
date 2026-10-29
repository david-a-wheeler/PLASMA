// expect: :3: error 63: function pointers are not supported yet: 'f'
int f() { return 0; }
int main() { return f; }
