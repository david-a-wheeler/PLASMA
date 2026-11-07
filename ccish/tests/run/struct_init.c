/* Struct initializers (M6 step 2): flat brace lists over the scalar
 * members, for globals, static locals and locals; brace elision across
 * arrays of structs; strings for char array members; zeros for what
 * the list doesn't reach; an array size from the list; and a local
 * initialized from another struct. */
struct pt { int x, y; };
struct item { char name[6]; int qty; struct pt *where; };
struct box { struct pt lo, hi; char tag; };

struct pt origin = {0};
struct pt corners[] = {1, 2, 3, 4, 5};      /* 3 points; the last y is 0 */
struct box gb = {1, 2, 3, 4, 'B'};
struct item stock[2] = {"nut", 40, &corners[1], "bolt"};
const struct pt unit = {1, 1};

/* Print a point. */
void show(const struct pt *p)
{
  __puti(p->x); __putc(','); __puti(p->y); __putc(' ');
}

int main()
{
  static struct pt saved = {7, 8};
  struct pt a = {10, 20};
  struct pt b = a;                /* a struct copy */
  struct box lb = {a.x, a.y + 1, 5};          /* the rest is 0 */
  struct pt many[2] = {0};
  int i;

  show(&origin);
  __puti(sizeof corners / sizeof corners[0]); __putc(' ');
  for (i = 0; i < 3; i++)
    show(&corners[i]);
  __putln();
  show(&gb.lo); show(&gb.hi); __putc(gb.tag); __putln();
  for (i = 0; i < 2; i++) {
    __puts(pstr("["));
    __puti(stock[i].qty); __putc(' ');
    __puti(stock[i].name[0]); __putc(' ');
    __puti(stock[i].where ? stock[i].where->y : -1);
    __puts(pstr("] "));
  }
  __putln();
  show(&unit); show(&saved); show(&b);
  show(&lb.lo); show(&lb.hi); __puti(lb.tag); __putln();
  show(&many[1]);
  __putln();
  return 0;
}
