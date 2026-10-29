// expect: :4: error 48: incompatible pointer types in initialization
int main() {
    int m[2][3];
    int *p = m;
    return 0;
}
