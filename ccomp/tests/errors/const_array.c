// expect: :7: error: a 'const' object can't be the left operand of assignment
/* The elements of a const array can't be assigned to. */
int main()
{
    const int t[2] = {1, 2};
    int i = 1;
    t[i] = 3;
    return 0;
}
