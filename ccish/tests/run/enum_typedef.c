/* enum and typedef (pulled forward from M11), used as ccish's own
 * source uses them, and with C's scope rules. */
// listing-has: CASES 3
enum { T_EOF, T_ID = 256, T_NUM, T_STR, };  /* a trailing comma */
enum { K_A = -2, K_B, K_C, K_D = K_C + 10, K_E };
enum color { RED, GREEN, BLUE, NCOLORS };

typedef struct Sym Sym;             /* before the struct is defined */
struct Sym { char name[4]; int kind; Sym *next; };

typedef struct {
  int op, a;
} Mop;

typedef const char *str;
typedef int Vec[3];
typedef Mop *MopP;

int counts[NCOLORS];                /* an enum constant as array size */
int start = T_NUM * 2;              /* ... and in a global initializer */
enum color fav = BLUE;
Sym table[2];

/* Name a token kind. */
str token_name(int t)
{
  switch (t) {
  case T_ID:  return "id";
  case T_NUM: return "num";
  case T_STR: return "str";
  }
  return "?";
}

/* Sum a vector. */
int sum(const Vec v)
{
  return v[0] + v[1] + v[2];
}

typedef int T;

/* A variable named like a typedef hides it, and a typedef in a block
 * hides a variable; both end with their block. */
int scopes(void)
{
  T a = 1;
  int r;
  {
    int T = 10;                 /* T is a variable here */
    r = T + a;
    {
      typedef char T;         /* and a type again here */
      T c = 300;              /* a char: 44 */
      r += c;
    }
    r += T;                     /* the variable */
  }
  {
    enum { a = 1000 };          /* hides the variable a */
    r += a;
  }
  return r + a + sizeof(T);       /* T is int again */
}

int main()
{
  Mop m, *p = &m;
  MopP q;
  Vec v = {1, 2, 3};
  const Vec cv = {4, 5, 6};
  Sym *s;
  enum color c = GREEN;
  T * x;                          /* declares x: T is a type */
  int i = 7;

  __puti(T_EOF); __putc(' '); __puti(T_STR); __putc(' ');
  __puti(K_A); __putc(','); __puti(K_C); __putc(','); __puti(K_E); __putc(' ');
  __puti(NCOLORS); __putc(' '); __puti(sizeof counts); __putc(' ');
  __puti(start); __putc(' '); __puti(fav + c); __putln();

  for (i = 255; i <= 258; i++) {
    __puts(pstr(" "));
    x = (T *)token_name(i);     /* a cast to a typedef */
    __puts((str)x - 1);           /* the PLASMA form, one byte before */
  }
  __putln();

  m.op = 5;
  q = p;
  q->a = m.op * 2;
  __puti(q->a); __putc(' ');
  __puti(sum(v) + sum(cv)); __putc(' ');
  __puti(sizeof(Vec) + sizeof(Mop) + sizeof(Sym)); __putc(' ');
  table[0].kind = T_ID;
  table[0].next = &table[1];
  table[1].kind = T_NUM;
  s = table;
  __puti(s->next->kind - s->kind); __putln();

  __puti(scopes());
  __putln();
  return 0;
}
