// expect: incompatible types in assignment
/* A struct can't be assigned an integer. */
struct pt { int x, y; };

int main()
{
    struct pt p;
    p = 0;
    return 0;
}
