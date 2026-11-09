// A module with no imports, so no CMDSYS dependency. Prints nothing;
// its exit status is f(1), 2 (no_imports.status).
int f(int a) { return a + 1; }
int main() { return f(1); }
