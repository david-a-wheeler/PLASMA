// expect: :4: error 55: wrong number of arguments in call to 'puts'
int main()
{
    puts(pstr("a"), 2);
    return 0;
}
