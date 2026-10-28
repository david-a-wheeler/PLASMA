// expect: :3: error: array too large (over 32767 bytes)
int main() {
    char a[200][200];
    return 0;
}
