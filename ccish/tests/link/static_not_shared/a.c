// b.c's static f isn't a.c's f.
int f(int n);
int main() { return f(1); }
