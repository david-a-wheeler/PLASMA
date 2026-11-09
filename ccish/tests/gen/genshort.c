// genshort.c: `short` is a synonym for `int`. The preprocessor (the
// pp_word of src/pplex.c) gives the word `short` the token of `int`,
// so the compiler sees only `int`, and a function written with
// `short` lowers to the same machine code as its `int` twin. Each
// short function here is paired with an int one of the same body, and
// the listing pins both: any divergence shows as a diff.
int iv = 5;
short sv = 6;

struct W { int x; int y; };
struct Ws { short x; short y; };

struct W gw;
struct Ws gws;

int f(int a, int b)         { return a + b; }
short fs(short a, short b)  { return a + b; }

int g(int *p)               { *p = *p + 1; return *p; }
short gs(short *p)          { *p = *p + 1; return *p; }

int h(int a)                { int s; s = a; s = s - a; return s; }
short hs(short a)           { short s; s = a; s = s - a; return s; }

int si(struct W *p)         { return p->x + p->y; }
short ss(struct Ws *p)      { return p->x + p->y; }

int main(void)
{
  gw.x = 1;
  gw.y = 2;
  gws.x = 3;
  gws.y = 4;
  return f(1, 2) + fs(1, 2) + g(&iv) + gs(&sv) + iv + sv
         + si(&gw) + ss(&gws) + h(3) + hs(3) == 42 ? 0 : 1;
}
