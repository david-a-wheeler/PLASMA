/* Clicking the Apple II speaker: reading $C030 toggles it. click() has
 * no parameters, locals or temporaries, so, as in PLASMA's compiler, it
 * has no frame: no ENTER, and RET instead of LEAVE. Its read through a
 * constant address is a single LAB, kept although its value is unused,
 * since it is I/O. ready() reads the keyboard at $C000 (bit 7 is set
 * when a key is waiting); its early return is RET plus a pad byte.
 * main() has no frame either. On plvm, $C000 and $C030 are plain memory
 * holding 0, so ready() is 0.
 *
 * The address is written as a cast for now. Once global initializers
 * and const exist (M5 steps 2 and 6), it can be a named constant,
 * volatile char *const SPEAKER = (volatile char *)0xC030, and should
 * give the same code (approach.md, "volatile is accepted"). */
// listing-has: LAB $C030
// listing-has: LAB $C000
// listing-lacks: ENTER
// listing-lacks: LEAVE
// listing-lacks: CW $C0
// listing-count: 6 RET
void click(void)
{
    *(char *)0xC030;
}

int ready(void)
{
    if (*(char *)0xC000 > 127)
        return 1;
    return 0;
}

int main()
{
    click();
    click();
    click();
    puti(ready());
    putln();
    return 0;
}
