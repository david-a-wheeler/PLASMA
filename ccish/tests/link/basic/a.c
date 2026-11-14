// Linking two objects (approach.md issue 11, "Step 3"): functions and
// globals defined in one object and used in the other, both ways; an
// address in another object's array, in data and in code; and static
// functions and globals of the same names in both objects, each kept
// to its own object.
// listing-has: object tests/out/link/basic_a:
// listing-has: object tests/out/link/basic_b:
// listing-has: ; add
// listing-has: ; helper
// listing-has: ; shared names: 12 of 307 slots, 92 of 3000 name bytes
extern int total;                   // defined in b.c
extern int squares[];               // b.c gives the size
int add(int n);                     // b.c
int b_hidden();                     // b.c
int scale = 3;                      // used by b.c
int *third = &squares[3];           // another object's array, in data
static int helper(int n) { return n + 100; }   // b.c has its own
static int hidden = 1;              // b.c has its own

// Called from b.c.
int triple(int n) { return n * scale; }

int main()
{
  add(4);
  add(5);
  __cmd_puti(total); __cmd_putc(' ');
  __cmd_puti(*third); __cmd_putc(' ');
  __cmd_puti(squares[2]); __cmd_putc(' ');
  __cmd_puti(helper(1)); __cmd_putc(' ');
  __cmd_puti(b_hidden() + hidden); __cmd_putc(' ');
  __cmd_puti(squares[4]);
  __cmd_putln();
  return 0;
}
