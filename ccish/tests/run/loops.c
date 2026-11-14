// Loops with the test at the bottom (approach.md, "Loop design"):
// for and while, nested, with break and continue, with no condition or
// no step, with calls, strings, && and ||, ?: and unsigned comparisons
// in the test and the step, a step with side effects, a header too big
// for ccgen's side buffer, and loops nested deeper than it holds.
// listing-lacks: CB 97
struct node {
  int v;
  struct node *next;
};

struct node n3 = {3, 0};
struct node n2 = {2, &n3};
struct node n1 = {1, &n2};
int calls;

void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

// Count calls, and return n.
int f(int n)
{
  calls++;
  return n;
}

int main()
{
  int i, j, k, s, a = 1;
  unsigned u;
  struct node *p;
  char *t;

  // Nested, with break and continue in each kind.
  s = 0;
  for (i = 0; i < 3; i++) {
    if (i == 1)
      continue;
    j = 0;
    while (j < 10) {
      j++;
      if (j == 2)
        continue;
      if (j > 3)
        break;
      s = s * 10 + j;
    }
  }
  show(s);                      // 1313
  // No condition, then no step.
  for (i = 0;; i++)
    if (i == 5)
      break;
  show(i);                      // 5
  for (i = 0; i < 3;)
    i += 2;
  show(i);                      // 4
  // Never run; the body's code is dropped.
  while (0)
    show(97);
  for (i = 9; i < 3; i++)
    show(i);
  // Endless, left by break.
  i = 0;
  while (1) {
    if (++i == 3)
      break;
  }
  show(i);                      // 3
  __cmd_putln();
  // Calls, && and ||, and ?: in the test and the step.
  calls = 0;
  s = 0;
  for (i = f(0); f(i) < 3 && (i != 9 || s); i = i ? i + 1 : 1)
    s += i;
  show(s);                      // 3
  show(calls);                  // 5
  // A string in the test; unsigned comparisons.
  t = "abc";
  for (i = 0; t[i] != 0 && "xyz"[i] != 'z'; i++)
    __cmd_putc(t[i]);
  __cmd_putc(' ');                    // ab
  s = 0;
  for (u = 0xFFF0; u > 0x7FFF; u += 4)
    s++;
  show(s);                      // 4
  s = 0;
  for (u = 0x8000; u >= 0x100; u = u / 2)
    s++;
  show(s);                      // 8
  // A step with side effects.
  s = 0;
  for (p = &n1; p; p = p->next)
    s = s * 10 + p->v;
  show(s);                      // 123
  // do while is unchanged.
  i = 0;
  do
    i += 3;
  while (i < 10);
  show(i);                      // 12
  __cmd_putln();
  // A header of more than 64 ops: laid out in place.
  s = 0;
  for (i = 0; i < a + a + a + a + a + a + a + a + a + a + a + a + a + a
               + a + a + a + a + a + a + a + a + a + a + a + a + a + a
               + a + a + a + a + a + a + a - 32; i = i + a - a + a - a
                                                     + a - a + 1)
    s += i;
  show(s);                      // 3
  // Loops nested deeper than ccgen holds (8).
  s = 0;
  for (i = 0; i < 2; i++)
    for (j = 0; j < 1; j++)
      for (k = 0; k < 1; k++)
        while (s < 100)
          while (s < 100)
            for (a = 0; a < 1; a++)
              for (; a < 1;)
                while (a < 1)
                  for (u = 0; u < 2; u++) {
                    s++;
                    a = 1;
                    if (u == 1)
                      s += 10;
                  }
  show(s);                      // 108
  __cmd_putln();
  return 0;
}
