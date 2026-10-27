/* Local variables: declarations with and without initializers, several
 * per declaration, initializers using earlier variables, block scope,
 * shadowing, and assignment operators.
 * f's frame: n (2 bytes) + a, b (4) + the larger of its two sibling
 * blocks, which share frame space (x: 2; y, z and the inner a: 6) = 12. */
// listing-has: ENTER 12,1
int f(int n)
{
    int a = n + 1, b;
    b = a * 2;
    {
        int x = 100;
        puti(x + a); putc(' ');
    }
    {
        int y = 1, z = 2;
        int a = 1000;               /* shadows the outer a */
        puti(a + y + z); putc(' ');
    }
    puti(a); putc(' ');             /* the outer a again */
    return b;
}

int main()
{
    int r;
    r = f(4);
    puti(r); putln();
    {
        int u, v, w;
        u = v = w = 7;              /* right-associative */
        puti(u + v + w); putc(' ');
        u += 3;  puti(u); putc(' ');
        u -= 4;  puti(u); putc(' ');
        u *= 5;  puti(u); putc(' ');
        u /= 4;  puti(u); putc(' ');
        u %= 4;  puti(u); putc(' ');
        u <<= 3; puti(u); putc(' ');
        u >>= 1; puti(u); putc(' ');
        u &= 10; puti(u); putc(' ');
        u |= 3;  puti(u); putc(' ');
        u ^= 15; puti(u); putc(' ');
        puti(v = 3); putc(' ');     /* an assignment's value */
        puti((w) = v + 1); putln(); /* a parenthesized variable */
    }
    return 0;
}
