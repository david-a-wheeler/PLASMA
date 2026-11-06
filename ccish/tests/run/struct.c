/* Structs (M6 step 1): definitions at file and block scope, members of
 * every kind, . and ->, arrays of structs, pointers to structs, a
 * linked list through an incomplete type, &, sizeof, and const
 * members. */
struct pt { int x, y; };

struct rec {
  char tag;               /* offset 0: no padding after a char */
  int n;                  /* offset 1 */
  char name[4];           /* offset 3 */
  struct pt at;           /* offset 7 */
  struct rec *next;       /* offset 11 */
};                          /* size 13 */

struct pt origin;           /* a global: zero */
struct pt pts[3];
struct rec recs[2];

/* Sum the n of each rec in a list. */
int total(struct rec *r)
{
  int t = 0;
  while (r) {
    t += r->n;
    r = r->next;
  }
  return t;
}

/* Set p's members, through a pointer. */
void move(struct pt *p, int x, int y)
{
  p->x = x;
  p->y = y;
}

int main()
{
  struct pt a;
  struct rec r;
  struct pt *p = &a;
  static struct pt saved;
  int i;

  /* sizes and offsets */
  puti(sizeof(struct pt)); putc(' ');
  puti(sizeof(struct rec)); putc(' ');
  puti(sizeof r.name); putc(' ');
  puti(sizeof pts); putc(' ');
  puti((char *)&r.at.y - (char *)&r); putln();

  /* a local, written and read directly and through a pointer */
  a.x = 3;
  a.y = 4;
  puti(a.x * a.y); putc(' ');
  p->x += 10;
  p->y++;
  puti(a.x); putc(' ');
  puti(p->y); putc(' ');
  puti(origin.x + origin.y); putln();

  /* nested members and an array member */
  r.tag = 'R';
  r.n = 300;
  r.name[0] = 'a';
  r.name[1] = 'b';
  r.name[2] = 0;
  r.at.x = 7;
  r.at.y = r.at.x * 2;
  putc(r.tag);
  puti(r.n); putc(' ');
  putc(r.name[0]); putc(r.name[1]); putc(' ');
  puti(r.at.y); putln();

  /* arrays of structs, with constant and variable indexes */
  for (i = 0; i < 3; i++)
    move(&pts[i], i, i * i);
  pts[2].x += 100;
  for (i = 0; i < 3; i++) {
    puti(pts[i].x); putc(','); puti(pts[i].y); putc(' ');
  }
  puti((pts + 1)->y); putln();

  /* a linked list */
  recs[0].n = 1;
  recs[1].n = 20;
  r.next = &recs[0];
  recs[0].next = &recs[1];
  recs[1].next = 0;
  puti(total(&r)); putc(' ');
  puti(r.next->next->n); putln();

  /* a static local keeps its value; a block-scope struct shadows */
  saved.y = 9;
  {
    struct pt { char c; } inner;
    inner.c = 'z';
    putc(inner.c); puti(sizeof inner); putc(' ');
  }
  puti(sizeof(struct pt)); putc(' ');
  puti(saved.y); putln();
  return 0;
}
