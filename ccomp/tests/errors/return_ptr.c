// expect: :2: error: integer from pointer without a cast in return
int f(int *p) { return p; }
int main() { return 0; }
