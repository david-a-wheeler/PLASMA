// Structs (M6 step 1): definitions at file and block scope, members of
// every kind, . and ->, arrays of structs, pointers to structs, a
// linked list through an incomplete type, &, sizeof, and const
// members.
struct pt { int x, y; };

struct rec {
  char tag;               // offset 0: no padding after a char
  int n;                  // offset 1
  char name[4];           // offset 3
  struct pt at;           // offset 7
  struct rec *next;       // offset 11
};                          // size 13

struct pt origin;           // a global: zero
struct pt pts[3];
struct rec recs[2];

// Sum the n of each rec in a list.
int total(struct rec *r)
{
  int t = 0;
  while (r) {
    t += r->n;
    r = r->next;
  }
  return t;
}

// Set p's members, through a pointer.
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

  // sizes and offsets
  __puti(sizeof(struct pt)); __putc(' ');
  __puti(sizeof(struct rec)); __putc(' ');
  __puti(sizeof r.name); __putc(' ');
  __puti(sizeof pts); __putc(' ');
  __puti((char *)&r.at.y - (char *)&r); __putln();

  // a local, written and read directly and through a pointer
  a.x = 3;
  a.y = 4;
  __puti(a.x * a.y); __putc(' ');
  p->x += 10;
  p->y++;
  __puti(a.x); __putc(' ');
  __puti(p->y); __putc(' ');
  __puti(origin.x + origin.y); __putln();

  // nested members and an array member
  r.tag = 'R';
  r.n = 300;
  r.name[0] = 'a';
  r.name[1] = 'b';
  r.name[2] = 0;
  r.at.x = 7;
  r.at.y = r.at.x * 2;
  __putc(r.tag);
  __puti(r.n); __putc(' ');
  __putc(r.name[0]); __putc(r.name[1]); __putc(' ');
  __puti(r.at.y); __putln();

  // arrays of structs, with constant and variable indexes
  for (i = 0; i < 3; i++)
    move(&pts[i], i, i * i);
  pts[2].x += 100;
  for (i = 0; i < 3; i++) {
    __puti(pts[i].x); __putc(','); __puti(pts[i].y); __putc(' ');
  }
  __puti((pts + 1)->y); __putln();

  // a linked list
  recs[0].n = 1;
  recs[1].n = 20;
  r.next = &recs[0];
  recs[0].next = &recs[1];
  recs[1].next = 0;
  __puti(total(&r)); __putc(' ');
  __puti(r.next->next->n); __putln();

  // a static local keeps its value; a block-scope struct shadows
  saved.y = 9;
  {
    struct pt { char c; } inner;
    inner.c = 'z';
    __putc(inner.c); __puti(sizeof inner); __putc(' ');
  }
  __puti(sizeof(struct pt)); __putc(' ');
  __puti(saved.y); __putln();
  return 0;
}
