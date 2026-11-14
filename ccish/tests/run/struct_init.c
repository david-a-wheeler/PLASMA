// Struct initializers (M6 step 2): flat brace lists over the scalar
// members, for globals, static locals and locals; brace elision across
// arrays of structs; strings for char array members; zeros for what
// the list doesn't reach; an array size from the list; and a local
// initialized from another struct.
struct pt { int x, y; };
struct item { char name[6]; int qty; struct pt *where; };
struct box { struct pt lo, hi; char tag; };

struct pt origin = {0};
struct pt corners[] = {1, 2, 3, 4, 5};      // 3 points; the last y is 0
struct box gb = {1, 2, 3, 4, 'B'};
struct item stock[2] = {"nut", 40, &corners[1], "bolt"};
const struct pt unit = {1, 1};

// Print a point.
void show(const struct pt *p)
{
  __cmd_puti(p->x); __cmd_putc(','); __cmd_puti(p->y); __cmd_putc(' ');
}

int main()
{
  static struct pt saved = {7, 8};
  struct pt a = {10, 20};
  struct pt b = a;                // a struct copy
  struct box lb = {a.x, a.y + 1, 5};          // the rest is 0
  struct pt many[2] = {0};
  int i;

  show(&origin);
  __cmd_puti(sizeof corners / sizeof corners[0]); __cmd_putc(' ');
  for (i = 0; i < 3; i++)
    show(&corners[i]);
  __cmd_putln();
  show(&gb.lo); show(&gb.hi); __cmd_putc(gb.tag); __cmd_putln();
  for (i = 0; i < 2; i++) {
    __cmd_puts(pstr("["));
    __cmd_puti(stock[i].qty); __cmd_putc(' ');
    __cmd_puti(stock[i].name[0]); __cmd_putc(' ');
    __cmd_puti(stock[i].where ? stock[i].where->y : -1);
    __cmd_puts(pstr("] "));
  }
  __cmd_putln();
  show(&unit); show(&saved); show(&b);
  show(&lb.lo); show(&lb.hi); __cmd_puti(lb.tag); __cmd_putln();
  show(&many[1]);
  __cmd_putln();
  return 0;
}
