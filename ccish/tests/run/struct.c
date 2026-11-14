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
  __cmd_puti(sizeof(struct pt)); __cmd_putc(' ');
  __cmd_puti(sizeof(struct rec)); __cmd_putc(' ');
  __cmd_puti(sizeof r.name); __cmd_putc(' ');
  __cmd_puti(sizeof pts); __cmd_putc(' ');
  __cmd_puti((char *)&r.at.y - (char *)&r); __cmd_putln();

  // a local, written and read directly and through a pointer
  a.x = 3;
  a.y = 4;
  __cmd_puti(a.x * a.y); __cmd_putc(' ');
  p->x += 10;
  p->y++;
  __cmd_puti(a.x); __cmd_putc(' ');
  __cmd_puti(p->y); __cmd_putc(' ');
  __cmd_puti(origin.x + origin.y); __cmd_putln();

  // nested members and an array member
  r.tag = 'R';
  r.n = 300;
  r.name[0] = 'a';
  r.name[1] = 'b';
  r.name[2] = 0;
  r.at.x = 7;
  r.at.y = r.at.x * 2;
  __cmd_putc(r.tag);
  __cmd_puti(r.n); __cmd_putc(' ');
  __cmd_putc(r.name[0]); __cmd_putc(r.name[1]); __cmd_putc(' ');
  __cmd_puti(r.at.y); __cmd_putln();

  // arrays of structs, with constant and variable indexes
  for (i = 0; i < 3; i++)
    move(&pts[i], i, i * i);
  pts[2].x += 100;
  for (i = 0; i < 3; i++) {
    __cmd_puti(pts[i].x); __cmd_putc(','); __cmd_puti(pts[i].y); __cmd_putc(' ');
  }
  __cmd_puti((pts + 1)->y); __cmd_putln();

  // a linked list
  recs[0].n = 1;
  recs[1].n = 20;
  r.next = &recs[0];
  recs[0].next = &recs[1];
  recs[1].next = 0;
  __cmd_puti(total(&r)); __cmd_putc(' ');
  __cmd_puti(r.next->next->n); __cmd_putln();

  // a static local keeps its value; a block-scope struct shadows
  saved.y = 9;
  {
    struct pt { char c; } inner;
    inner.c = 'z';
    __cmd_putc(inner.c); __cmd_puti(sizeof inner); __cmd_putc(' ');
  }
  __cmd_puti(sizeof(struct pt)); __cmd_putc(' ');
  __cmd_puti(saved.y); __cmd_putln();
  return 0;
}
