/* A function's address can initialize a pointer, but not an int: cast
 * to an int, it isn't a constant ccish (or the loader) can relocate. */
int f(int a) { return a; }
int x = (int)f;
int main() { return 0; }
