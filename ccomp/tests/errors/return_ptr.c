// expect: :2: error 51: integer from pointer without a cast in return
int f(int *p) { return p; }
int main() { return 0; }
