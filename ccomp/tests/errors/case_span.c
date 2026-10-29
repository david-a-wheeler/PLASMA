// expect: case values too far apart (more than 32767)
/* SEL's case values must span at most 32767 on the 6502 VM
 * (approach.md, "M7 design"). */
int main()
{
    int x = 1;
    switch (x) {
    case -20000: x = 2; break;
    case 20000: x = 3; break;
    }
    return x;
}
