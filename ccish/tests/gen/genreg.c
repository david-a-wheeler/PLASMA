// genreg.c - what register (IR_REGLOCAL) lets ccgen do (irloop.c): no
// pointer can reach a register local, since C forbids taking its
// address, so code later in an outer loop can't change it. So a loop
// nested in another over register locals may be counted though its
// body stores through a pointer; may end at != (genne.c); and has its
// costly invariant expressions hoisted (hoist()). Each function says
// what it pins; those that mustn't be optimized give a different sum
// if they were.
int n;
char buf[12];

// Count n up by x.
void tick(int x) { n += x; }

// The inner loop is counted (CN 4 ... BRLT): i is register, and its
// limit a constant, so its store through p is safe.
void fill(char *p)
{
  register int i, j;
  for (j = 0; j < 3; j++)
    for (i = 0; i < 4; i++)
      p[j * 4 + i] = i + j;
}

// Not counted: i isn't register, and the body stores through p.
void fill_plain(char *p)
{
  int i, j;
  for (j = 0; j < 3; j++)
    for (i = 0; i < 4; i++)
      p[j * 4 + i] = i + j;
}

// The inner loop calls, so isn't counted, but ends CN 5 BRNE.
void ne_reg(void)
{
  register int i, j;
  for (j = 0; j < 2; j++)
    for (i = 0; i <= 4; i++)
      tick(i);
}

// x * y / 3 is hoisted: computed before the loop into a temporary.
int h_reg(int a, int b)
{
  register int x = a, y = b, i, s = 0;
  for (i = 0; i < 5; i++)
    s += x * y / 3 + i;
  return s;
}

// Hoisted too: an outermost loop, a and b's addresses never taken.
int h_outer(int a, int b)
{
  int i, s = 0;
  for (i = 0; i < 5; i++)
    s += a * b / 3 + i;
  return s;
}

// Not: a divide by a variable might fail where the loop wouldn't run it.
int h_div(int a, int b)
{
  int i, s = 0;
  for (i = 0; i < 5; i++)
    s += a / b + i;
  return s;
}

// Not: a and b aren't register, in a nested loop; the store through q,
// once q = &a later in the outer loop, changes a (a hoisted a * b
// would give 24, not 39).
int h_nested(void)
{
  int a = 2, b = 3, s = 0, dummy, i, j, *q = &dummy;
  for (j = 0; j < 2; j++) {
    for (i = 0; i < 2; i++) {
      s += a * b;
      *q = 7;
    }
    q = &a;
  }
  return s;
}

// fill and fill_plain each sum to 30 (0..3, 1..4, 2..5: 6 + 10 + 14);
// ne_reg ticks
// 20; h_reg and h_outer are 5 * 2 + 10 = 20 each (x * y / 3 is 2);
// h_div 5 * 3 + 10 = 25; h_nested 39.
int main(void)
{
  int k, s1 = 0, s2 = 0;
  fill(buf);
  for (k = 0; k < 12; k++)
    s1 += buf[k];
  fill_plain(buf);
  for (k = 0; k < 12; k++)
    s2 += buf[k];
  ne_reg();
  return s1 == 30 && s2 == 30 && n == 20 && h_reg(2, 3) == 20
         && h_outer(2, 3) == 20 && h_div(6, 2) == 25 && h_nested() == 39;
}
