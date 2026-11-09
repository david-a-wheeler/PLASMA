// The other half of basic: see a.c.
extern int scale;
int triple(int n);
int total;
int squares[5] = {0, 1, 4, 9, 16};
int *pscale = &scale;               // a.c's global, in data
static int helper(int n) { return n + 200; }
static int hidden = 2;

// Add triple(n) to the total.
int add(int n)
{
  total += triple(n) + helper(0) - 200;
  return total;
}

// This object's own statics, and a.c's scale through a pointer.
int b_hidden()
{
  return hidden * 10 + helper(0) - 200 + *pscale - 3;
}
