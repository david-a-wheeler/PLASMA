// expect: incompatible types in initialization
/* A local struct initialized from a struct of another type. */
struct a { int x; };
struct b { int x; };

int main()
{
    struct a p;
    struct b q = p;
    return 0;
}
