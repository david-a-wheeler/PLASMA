// expect: 'continue' is not inside a loop
/* continue in a switch goes to the loop around it, so it needs one. */
int main()
{
    int x = 1;
    switch (x) {
    case 1: continue;
    }
    return x;
}
