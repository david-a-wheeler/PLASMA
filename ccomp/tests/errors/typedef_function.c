// expect: typedefs of function types are not supported
/* There are no function types to name (no function pointers yet). */
typedef int F(int);

int main()
{
    return 0;
}
