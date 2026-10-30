// expect: :3: error 217: static declaration follows a non-static declaration of 'f'
int f(void);
static int f(void) { return 1; }
int main() { return f(); }
