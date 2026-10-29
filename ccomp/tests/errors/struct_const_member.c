// expect: a 'const' object can't be the operand of ++ or --
/* A const member can't be changed, even in a struct that isn't const. */
struct counter { const int id; int n; };

int main()
{
    struct counter c;
    c.n++;
    c.id++;
    return 0;
}
