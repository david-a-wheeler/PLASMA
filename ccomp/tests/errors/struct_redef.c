// expect: redefinition of 'struct pt'
/* A tag can be defined once in a scope. */
struct pt { int x, y; };
struct pt { int x, y; };

int main()
{
    return 0;
}
