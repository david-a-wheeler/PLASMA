/* A function isn't an lvalue: it can't be assigned to. */
int f(int);
int main() { f = 0; return 0; }
