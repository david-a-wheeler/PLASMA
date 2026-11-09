// A function's value is a pointer to it: returning it as an int needs
// a cast.
int f() { return 0; }
int main() { return f; }
