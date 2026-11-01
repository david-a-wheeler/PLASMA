/* Counted loops (approach.md, "Loop design", step 2): each form, with
 * the init folded into the count or not, both ends constant, the final
 * value of the variable after a normal exit, a break, and a loop that
 * never runs; continue and a switch in the body. The bodies make no
 * calls; results are shown after each loop. The counts pin which loops
 * are counted, in which form (counted_not.c has those that mustn't
 * be). */
// Each loop, by function: up: BRLT; INCBRLE; BRGT, INCBRLE; BRLT;
// BRGT, ADDBRLE. down: DECBRGE; BRLT, DECBRGE; BRGT; BRLT, SUBBRGE.
// never: BRLT 3 times (the two loops with both ends constant that
// never run aren't counted). flow: BRLT; INCBRLE. nested: BRLT.
// listing-count: 3 INCBRLE
// listing-count: 1 ADDBRLE
// listing-count: 2 DECBRGE
// listing-count: 1 SUBBRGE
// listing-count: 9 BRLT
// listing-count: 3 BRGT
void show(int n)
{
  puti(n);
  putc(' ');
}

/* Counting up. */
void up(void)
{
  int i, j, n, s;
  n = 5;
  s = 0;
  for (i = 0; i < n; i++)       /* BRLT, local limit */
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = 0; i < 10; i++)      /* INCBRLE, both ends constant */
    s += i;
  show(s);
  show(i);
  j = 3;
  s = 0;
  for (i = j; i <= n; i++)      /* INCBRLE, init from a local */
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = 1; i < 11; i += 3)   /* ADDI 3, BRLT */
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = 0; i <= n; i += 2)   /* ADDBRLE */
    s += i;
  show(s);
  show(i);
  putln();
}

/* Counting down. */
void down(void)
{
  int i, n = 5, s, t;
  s = 0;
  for (i = 4; i > 0; i--)       /* DECBRGE, both ends constant */
    s = s * 10 + i;
  show(s);
  show(i);
  s = 0;
  for (i = n; i >= 2; i--)      /* DECBRGE */
    s += i;
  show(s);
  show(i);
  s = 0;
  t = 1;
  for (i = 9; i > t; i -= 3)    /* SUBI 3, BRGT */
    s += i;
  show(s);
  show(i);
  s = 0;
  for (i = 10; i >= t; i -= 4)  /* SUBBRGE */
    s += i;
  show(s);
  show(i);
  putln();
}

/* The limit first; loops that never run. */
void never(void)
{
  int i, n = 5, s;
  s = 0;
  for (i = 0; n > i; i++)
    s++;
  show(s);
  show(i);
  s = 0;
  for (i = 7; i < 3; i++)
    s++;
  show(s);
  show(i);
  i = 8;
  for (; i < n; i++)
    s++;
  show(s);
  show(i);
  n = -32767 - 1;
  for (i = 0; i < n; i++)
    s++;
  show(s);
  show(i);
  for (i = 0; i < -32767 - 1; i++)
    s++;
  show(s);
  show(i);
  putln();
}

/* break, continue, a switch. */
void flow(void)
{
  int i, n, s;
  s = 0;
  n = 10;
  for (i = 0; i < n; i++) {
    if (i == 2)
      continue;
    if (i == 6)
      break;
    s += i;
  }
  show(s);
  show(i);
  s = 0;
  for (i = 0; i < 6; i++)
    switch (i) {
    case 1:
      s += 10;
      break;
    case 4:
      continue;
    default:
      s++;
    }
  show(s);
  show(i);
  putln();
}

/* An inner loop that is counted (its outer loop holds a loop, so it
 * isn't). */
void nested(void)
{
  int i, j, s;
  s = 0;
  for (j = 0; j < 3; j++)
    for (i = 0; i < j; i++)
      s += 10 * j + i;
  show(s);
  show(i);
  putln();
}

int main()
{
  up();
  down();
  never();
  flow();
  nested();
  return 0;
}
