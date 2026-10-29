// expect: :6: error 48: incompatible pointer types in argument 1 of 'trace'
int trace(int m[][3]);
int main() {
    int m[3][4];
    /* m is a pointer to arrays of 4 ints, not of 3 */
    return trace(m);
}
