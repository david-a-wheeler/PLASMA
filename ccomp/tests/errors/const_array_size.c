// expect: a const variable's value isn't a constant expression in C (an array size must be one)
/* In C, a const variable isn't a constant expression, so it can't size
 * an array (C99 would make this a variable-length array, which ccish
 * doesn't have). ccish refuses it, so its programs stay valid C. */
int main()
{
    const int N = 3;
    int a[N];
    return 0;
}
