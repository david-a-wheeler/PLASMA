// Structs passed and returned by value (M6 step 3): up to 8 bytes, as
// the words of their bytes. A parameter lands in the callee's frame in
// its own layout, so it needs no copy; a returned struct is its words
// on the stack (a PLASMA multi-value return), stored straight into the
// variable it is assigned to.
struct pt { int x, y; };
struct rgb { char r, g, b; };               // 3 bytes: 2 words
struct qr { int q, r; };
struct name { char s[4]; };
struct wide { int a, b, c; char d, e; };    // 8 bytes: 4 words
struct one { int v; };

struct pt pts[3];

// A struct parameter is a copy: changing it changes nothing outside.
int sum(struct pt p)
{
  p.x += p.y;
  return p.x;
}

// Return two values, like PLASMA's divmod()#2.
struct qr divide(int a, int b)
{
  struct qr v;
  v.q = a / b;
  v.r = a % b;
  return v;
}

// Return a struct made from a parameter.
struct rgb dim(struct rgb c, int k)
{
  c.r = c.r / k;
  c.g = c.g / k;
  c.b = c.b / k;
  return c;
}

// Pass a returned struct straight on, and return one straight back.
struct pt swap(struct pt p)
{
  struct pt t;
  t.x = p.y;
  t.y = p.x;
  return t;
}

// Return the swapped point, unchanged: the call's words are returned
// as they are.
struct pt twice(struct pt p)
{
  return swap(swap(p));
}

// Return the 4-word struct with each member doubled.
struct wide dbl(struct wide w)
{
  w.a *= 2; w.b *= 2; w.c *= 2; w.d *= 2; w.e *= 2;
  return w;
}

// A one-word struct, through a pointer.
int get(struct one o)
{
  return o.v;
}

// Return a pointer to pts[i].
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

  __puti(sum(a)); __putc(' '); __puti(a.x); __putc(' ');
  __puti(sum(*p)); __putc(' ');
  d = divide(17, 5);
  __puti(d.q); __putc(' '); __puti(d.r); __putc(' ');
  __puti(divide(23, 4).r); __putln();

  e = dim(c, 2);
  __puti(e.r); __putc(' '); __puti(e.g); __putc(' '); __puti(e.b); __putc(' ');
  __puti(dim(dim(c, 2), 5).r); __putln();

  a = swap(a);
  __puti(a.x); __putc(','); __puti(a.y); __putc(' ');
  a = twice(a);
  __puti(a.x); __putc(','); __puti(a.y); __putc(' ');
  pts[1] = swap(a);
  p = &pts[2];
  *p = pts[1];
  *at(0) = swap(*p);
  for (i = 0; i < 3; i++) {
    __puti(pts[i].x); __putc(','); __puti(pts[i].y); __putc(' ');
  }
  i = 0;
  __puti(sum(pts[i + 1])); __putc(' ');
  __puti(get(*(struct one *)&o)); __putc(' ');
  swap(a);                        // the result is dropped
  __putln();

  w = dbl(w);
  __puti(w.a + w.b + w.c + w.d + w.e); __putc(' ');
  __puti(dbl(w).e); __putc(' ');
  __putc(n.s[0]); __puti(sizeof n); __putln();
  return 0;
}
