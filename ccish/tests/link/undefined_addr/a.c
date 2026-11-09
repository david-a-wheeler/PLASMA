// A function whose address is taken must be defined somewhere, as one
// that is called must.
int f(int n);
int main() { int (*p)(int) = f; return p != 0; }
