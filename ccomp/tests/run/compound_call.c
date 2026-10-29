/* x op= e, where e calls a function that moves the pointer x is reached
 * through. C evaluates x once, and a call runs entirely before or
 * entirely after that (C11 6.5.2.2p10, 6.5.16.2p3), so the load and the
 * store must use the same element: here the one the moved pointer
 * points to, since ccish makes the call first (approach.md 4(d2)). The
 * operator is then applied with its operands reversed: + as it is, -
 * as SUB then NEG, and the others with e saved in a temporary (so main's
 * frame is its one local plus one temporary). A pointer's right side is
 * scaled first, and a char gets its low byte. Without a call, the
 * usual order is kept. */
// listing-has: ENTER 4,0
// listing-has: NEG
int g[8];
int *gp;
int *ps[3];
int **pp;
char cs[3] = {10, 20, 30};
char *cp;

void show(int n)
{
    puti(n);
    putc(' ');
}

int next(int v)
{
    gp++;
    return v;
}

int movepp(int v)
{
    pp++;
    return v;
}

int movecp(int v)
{
    cp++;
    return v;
}

int main()
{
    int i;
    for (i = 0; i < 8; i++)
        g[i] = 100 + i;
    gp = g;
    *gp += next(5);
    *gp -= next(5);
    *gp *= next(2);
    *gp /= next(4);
    *gp %= next(10);
    *gp <<= next(1);
    *gp >>= next(1);
    for (i = 0; i < 8; i++)
        show(g[i]);
    gp = g;
    show(*gp -= next(1));
    show(g[1]);
    show(g[0]);
    ps[0] = g;
    ps[1] = g + 2;
    ps[2] = g + 6;
    pp = ps;
    *pp += movepp(1);
    *pp -= movepp(2);
    show(*ps[1]);
    show(*ps[2]);
    show(*ps[0]);
    cp = cs;
    *cp -= movecp(25);
    show(cs[1]);
    show(cs[0]);
    gp = g;
    i = 3;
    *gp += i * 2;
    show(g[0]);
    putln();
    return 0;
}
