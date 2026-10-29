// expect: a variable can't have the incomplete type 'struct s'
/* A variable needs a complete type, to have a size. */
struct s;

int main()
{
    struct s v;
    return 0;
}
