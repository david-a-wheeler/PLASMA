// expect: :3: error: declarators in parentheses are not supported yet
int main() {
    int a[4], (*p)[4] = &a;
    return 0;
}
