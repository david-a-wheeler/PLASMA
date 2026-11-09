// genne.c - a loop that can't be counted (a call in its body) but has a
// counted loop's shape, a step of 1 and a constant limit ends at !=
// (irloop.c, ne_candidate()): v <= 4 is CN 5 BRNE, one op, where ISLE
// BRTRU is two. Only an outermost loop with no goto label before it or
// in it, whose variable starts at a constant no further than the stop,
// isn't stored or taken in the body, and whose address isn't taken:
// in each function below that isn't so, != would run on past the stop.
int n;
void tick(int x) { n += x; }

// Each ends CN 5 BRNE (or for i > 1, CN 1, and for i >= 0, MINUS1).
void up_le(void) { int i; for (i = 0; i <= 4; i++) tick(i); }
void up_lt(void) { int i; for (i = 2; i < 5; i++) tick(i); }
void down_ge(void) { int i; for (i = 4; i >= 0; i--) tick(i); }
void down_gt(void) { int i; for (i = 4; i > 1; i--) tick(i); }

// Not: i starts past the stop (!= would run until it wraps).
void past(void) { int i; for (i = 7; i <= 4; i++) tick(i); }

// Not: the body stores i.
void changes(void)
{
  int i;
  for (i = 0; i <= 9; i++) {
    tick(i);
    if (i > 2)
      i = 20;
  }
}

// Not: i's address is taken.
void taken(void) { int i, *p = &i; for (i = 0; i <= 4; i++) tick(*p); }

// The outer loop is outermost, and nothing in it stores j: CN 2 BRNE;
// the inner one isn't (a pointer to i could be taken later in the
// outer loop): ISLE BRTRU.
void nested(void)
{
  int i, j;
  for (j = 0; j < 2; j++)
    for (i = 0; i <= 4; i++)
      tick(i);
}

// Not: i <= 32767 has no stop that fits.
void last(void)
{
  int i;
  for (i = 32765; i <= 32767; i++) {
    tick(1);
    if (i > 32766)
      break;
  }
}

// Not: after a goto label, which may run the loop again.
void after_goto(int k)
{
  int i;
again:
  for (i = 0; i <= 4; i++)
    tick(i);
  if (k-- > 0)
    goto again;
}

// Not the second loop, whose variable's address is taken, though the
// counted loop before it was a candidate: (with s != 2, its stop, the
// second would add 0 + 1, not 0 + 1 + 2).
void stale(void)
{
  int a[3], i, s, *p = &s;
  for (i = 0; i < 2; i++)
    a[i] = i;
  a[2] = 2;
  for (s = 0; s <= 2; s++)
    tick(a[s] + *p - s);
}

// 10 + 9 + 10 + 9 + 0 + 6 + 10 + 20 + 3 + 20 + 3 is 100.
int main(void)
{
  up_le(); up_lt(); down_ge(); down_gt(); past(); changes(); taken();
  nested(); last(); after_goto(1); stale();
  return n == 100;
}
