// expect: :8: error 42: a 'const' object can't be the left operand of assignment
/* What a const char * points to can't be changed through it, even by
 * a compound assignment. (The pointer itself can change.) */
int main()
{
    const char *p = "x";
    p++;
    *p += 1;
    return 0;
}
