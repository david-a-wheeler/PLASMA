// expect: :4: error: an array can't be the operand of ++ or --
int main() {
    int a[3];
    a++;
    return 0;
}
