// expect: :3: error 87: array size missing
/* Only the first size of an array parameter may be left out. */
int f(int m[][]);
int main() {
    return 0;
}
