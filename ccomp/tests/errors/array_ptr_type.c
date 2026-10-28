// expect: :4: error: incompatible pointer types in initialization
int main() {
    int m[2][3];
    int *p = m;
    return 0;
}
