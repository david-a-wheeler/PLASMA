/* extern (approach.md issue 11): globals declared extern and defined
 * later in the file, so that the references before the definitions are
 * data symbol relocations (kind 4, an extern global plus an addend),
 * which the final pass resolves against the file's own symbols; extern
 * arrays without a size; extern globals' addresses in initializers; an
 * extern nothing uses, and never defined; and static and extern on
 * functions. */
extern int count;
extern int table[];
extern char name[];
struct pt { int x, y; };
extern struct pt origin;
extern int never_defined;           /* not used, so not needed */
int *pcount = &count;               /* data: count + 0 */
int *pthird = &table[3];            /* data: table + 6 */
char *pname = name;                 /* an array's name */
int *pend = table + 5;              /* one past the end */
static int helper(int n);
extern int twice(int n);

int main()
{
  count = 5;
  count += 2;
  table[2] = 40;
  table[count - 4] = 30;            /* table[3] */
  origin.y = 9;
  puti(count); putc(' ');
  puti(*pcount); putc(' ');
  puti(table[2] + table[3]); putc(' ');
  puti(*pthird); putc(' ');
  puti(pend - pthird); putc(' ');
  puti(origin.x + origin.y); putc(' ');
  putc(pname[0]); putc(name[1]); putc(' ');
  puti(helper(twice(3)));
  putln();
  return 0;
}

int count = 1;
int table[5] = {1, 2, 3, 4, 5};
char name[] = "hi";
struct pt origin = {1, 2};
extern int count;                   /* again, after the definition */
static int helper(int n) { return n + 1; }
int twice(int n) { return n * 2; }
