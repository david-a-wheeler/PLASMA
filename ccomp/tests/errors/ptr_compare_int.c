// expect: :2: error 83: comparison between pointer and integer
int main() { int x, *p = &x; return p < 1; }
