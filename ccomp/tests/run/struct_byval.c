/* Structs passed and returned by value (M6 step 3): up to 8 bytes, as
 * the words of their bytes. A parameter lands in the callee's frame in
 * its own layout, so it needs no copy; a returned struct is its words
 * on the stack (a PLASMA multi-value return), stored straight into the
 * variable it is assigned to. */
struct pt { int x, y; };
struct rgb { char r, g, b; };               /* 3 bytes: 2 words */
struct qr { int q, r; };
struct name { char s[4]; };
struct wide { int a, b, c; char d, e; };    /* 8 bytes: 4 words */
struct one { int v; };

struct pt pts[3];

/* A struct parameter is a copy: changing it changes nothing outside. */
int sum(struct pt p)
{
    p.x += p.y;
    return p.x;
}

/* Return two values, like PLASMA's divmod()#2. */
struct qr divide(int a, int b)
{
    struct qr v;
    v.q = a / b;
    v.r = a % b;
    return v;
}

/* Return a struct made from a parameter. */
struct rgb dim(struct rgb c, int k)
{
    c.r = c.r / k;
    c.g = c.g / k;
    c.b = c.b / k;
    return c;
}

/* Pass a returned struct straight on, and return one straight back. */
struct pt swap(struct pt p)
{
    struct pt t;
    t.x = p.y;
    t.y = p.x;
    return t;
}

/* Return the swapped point, unchanged: the call's words are returned
 * as they are. */
struct pt twice(struct pt p)
{
    return swap(swap(p));
}

/* Return the 4-word struct with each member doubled. */
struct wide dbl(struct wide w)
{
    w.a *= 2; w.b *= 2; w.c *= 2; w.d *= 2; w.e *= 2;
    return w;
}

/* A one-word struct, through a pointer. */
int get(struct one o)
{
    return o.v;
}

/* Return a pointer to pts[i]. */
struct pt *at(int i)
{
    return &pts[i];
}

int main()
{
    struct pt a = {3, 4}, *p = &a;
    struct qr d;
    struct rgb c = {200, 100, 50}, e;
    struct name n = {"abc"};
    struct wide w = {1, 2, 3, 4, 5};
    struct one o = {77};
    int i = 0;

    puti(sum(a)); putc(' '); puti(a.x); putc(' ');
    puti(sum(*p)); putc(' ');
    d = divide(17, 5);
    puti(d.q); putc(' '); puti(d.r); putc(' ');
    puti(divide(23, 4).r); putln();

    e = dim(c, 2);
    puti(e.r); putc(' '); puti(e.g); putc(' '); puti(e.b); putc(' ');
    puti(dim(dim(c, 2), 5).r); putln();

    a = swap(a);
    puti(a.x); putc(','); puti(a.y); putc(' ');
    a = twice(a);
    puti(a.x); putc(','); puti(a.y); putc(' ');
    pts[1] = swap(a);
    p = &pts[2];
    *p = pts[1];
    *at(0) = swap(*p);
    for (i = 0; i < 3; i++) {
        puti(pts[i].x); putc(','); puti(pts[i].y); putc(' ');
    }
    i = 0;
    puti(sum(pts[i + 1])); putc(' ');
    puti(get(*(struct one *)&o)); putc(' ');
    swap(a);                        /* the result is dropped */
    putln();

    w = dbl(w);
    puti(w.a + w.b + w.c + w.d + w.e); putc(' ');
    puti(dbl(w).e); putc(' ');
    putc(n.s[0]); puti(sizeof n); putln();
    return 0;
}
