// expect: a struct member can't have the incomplete type 'struct node'
/* A struct can't contain itself, only a pointer to itself. */
struct node { int val; struct node next; };

int main()
{
    return 0;
}
