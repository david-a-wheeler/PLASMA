// expect: a 'for' loop can't declare a static variable
/* C99 allows only automatic variables in a for loop's declaration. */
int main()
{
    for (static int i = 0; i < 3; i++)
        ;
    return 0;
}
