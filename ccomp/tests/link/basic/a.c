/* Linking two objects (approach.md issue 11, "Step 3"): functions and
 * globals defined in one object and used in the other, both ways; an
 * address in another object's array, in data and in code; and static
 * functions and globals of the same names in both objects, each kept
 * to its own object. */
// listing-has: object tests/out/link/basic_a:
// listing-has: object tests/out/link/basic_b:
// listing-has: ; add
// listing-has: ; helper
// listing-has: ; shared names: 12 of 149 slots, 74 of 1281 name bytes
extern int total;                   /* defined in b.c */
extern int squares[];               /* b.c gives the size */
int add(int n);                     /* b.c */
int b_hidden();                     /* b.c */
int scale = 3;                      /* used by b.c */
int *third = &squares[3];           /* another object's array, in data */
static int helper(int n) { return n + 100; }   /* b.c has its own */
static int hidden = 1;              /* b.c has its own */

/* Called from b.c. */
int triple(int n) { return n * scale; }

int main()
{
  add(4);
  add(5);
  puti(total); putc(' ');
  puti(*third); putc(' ');
  puti(squares[2]); putc(' ');
  puti(helper(1)); putc(' ');
  puti(b_hidden() + hidden); putc(' ');
  puti(squares[4]);
  putln();
  return 0;
}
