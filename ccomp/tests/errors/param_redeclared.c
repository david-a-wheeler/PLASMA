// expect: :3: error: redeclaration of 'n'
int f(int n) {
    int n;
    return n;
}
