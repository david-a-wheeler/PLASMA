// expect: :4: error: array subscript is not an integer
int main() {
    int a[3], *p = a;
    return a[p];
}
