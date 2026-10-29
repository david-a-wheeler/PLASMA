// expect: a 'const' object can't be the left operand of assignment
/* A const variable can't be assigned to, even though reading it
 * elsewhere gives its value at compile time. */
const int N = 1;

int main()
{
    N = 2;
    return 0;
}
