// expect: an enum value must be an integer constant
/* An enum value must be known at compile time. */
int x;
enum { A = x };

int main()
{
    return 0;
}
