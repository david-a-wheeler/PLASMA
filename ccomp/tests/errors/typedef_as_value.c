// expect: a type name can't be used as a value: 'T'
/* A typedef name is a type, not a value. */
typedef int T;

int main()
{
    return T + 1;
}
