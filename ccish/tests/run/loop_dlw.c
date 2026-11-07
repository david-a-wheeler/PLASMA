/* The DLW fix (approach.md, "The DLW fix", and "for any store"): a
 * loop that isn't counted, whose step ends by storing v and whose test
 * starts with v, enters by pushing v and jumping past the test's own
 * load, so the step keeps the v it stores (DLW, or DLB for a char).
 * Pointer, unsigned and int loops; != and < (with a call in the body,
 * so not counted); continue and break; an init just before the loop
 * (which fuses with the entry's push) and not; a loop that never runs;
 * steps that aren't v += k (a linked list, v = v * 2, a char); and a
 * loop the fix must leave alone (the variable second in the test). */
// Each of the 12 fixed int and pointer loops (ptrs 3, uns 2, calls 2,
// flow 1, never 2, stores 2) ends its step with a DLW, and 11 have
// another where their init meets the entry's push (all but never's
// second loop, whose init isn't right before it). stores' 2 char loops
// have a DLB each for their step, and for their init. The loop left
// alone has none. (" DLW": not ADDLW.)
// listing-count: 23  DLW
// listing-count: 4  DLB
struct node {
  int v;
  struct node *next;
};

struct node n3 = {3, 0};
struct node n2 = {2, &n3};
struct node n1 = {1, &n2};
int a[6] = {1, 2, 3, 4, 5, 6};

void show(int n)
{
  __puti(n);
  __putc(' ');
}

/* Pointer loops, up and down. */
void ptrs(void)
{
  int *p, *e = a + 6, s = 0;
  for (p = a; p != e; p++)
    s += *p;
  show(s);
  s = 0;
  for (p = e - 1; p != a + 2; p--)
    s = s * 10 + *p;
  show(s);
  s = 0;
  for (p = a; p != e; p += 2)
    s += *p;
  show(s);
}

/* An unsigned counter, through 0 and across $8000. */
void uns(void)
{
  unsigned u;
  int s = 0;
  for (u = 65533u; u != 2; u++)
    s++;
  show(s);
  show((int)u);
  s = 0;
  u = 32766u;
  for (; u < 32770u; u++)
    s++;
  show(s);
}

/* int loops with a call in the body (not counted): < and !=. */
void calls(void)
{
  int i;
  for (i = 0; i < 3; i++)
    show(i);
  for (i = 5; i != 2; i--)
    show(i);
  show(i);
}

/* continue and break. */
void flow(void)
{
  int i, s = 0;
  for (i = 0; i != 10; i++) {
    if (i == 3)
      continue;
    if (i == 6)
      break;
    show(i);
    s += i;
  }
  show(s);
  show(i);
}

/* Never runs: the start is the end. */
void never(void)
{
  int *p = a, s = 0;
  for (p = a; p != a; p++)
    s++;
  show(s);
  p = a + 3;
  s = 0;
  for (; p != a + 3; p++)
    s++;
  show(s);
  show(p - a);
}

/* Steps that end by storing the variable, besides v += k: a linked
 * list, v = v * 2, and a char, by c++ and by c = c + 1. */
void stores(void)
{
  struct node *q;
  int i, s = 0;
  char c;
  for (q = &n1; q; q = q->next)
    show(q->v);
  for (i = 1; i != 16; i = i * 2)
    s += i;
  show(s);
  for (c = 97; c != 101; c++)
    __putc(c);
  for (c = 'w'; c != 'z'; c = c + 1)
    __putc(c);
  __putc(' ');
}

/* Left alone: the variable second in the test (n > i); it must still
 * be right. */
void alone(void)
{
  int i, n = 4;
  for (i = 0; n > i; i++)
    show(i);
}

int main()
{
  ptrs();
  __putln();
  uns();
  __putln();
  calls();
  __putln();
  flow();
  __putln();
  never();
  __putln();
  stores();
  __putln();
  alone();
  __putln();
  return 0;
}
