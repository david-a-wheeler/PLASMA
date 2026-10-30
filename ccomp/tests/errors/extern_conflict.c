// expect: :3: error 165: conflicting declaration of 'x'
extern int x;
char x;
int main() { return x; }
