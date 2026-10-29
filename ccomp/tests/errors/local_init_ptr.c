// expect: :2: error 47: pointer from integer without a cast in initialization
int main() { int *p[1] = {5}; return 0; }
