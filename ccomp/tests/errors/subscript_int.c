// expect: :4: error 69: subscripted value is neither array nor pointer
int main() {
    int x = 1;
    return x[1];
}
