// expect: a struct can't be used as a value here
/* C has no == for structs. */
struct pt { int x, y; };

int main()
{
    struct pt a, b;
    return a == b;
}
