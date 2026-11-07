/* The code for && and || as jumps (approach.md C4, "Conditions as
 * jumps"). No comparison is turned into a 1 or 0 (no NEG), == and !=
 * fuse with their branch (BREQ, BRNE), and with no else part, no
 * BRNCH is left: the one that a && b || c and !(a && b) need goes, by
 * Bcond L BRNCH T L: -> Binv T L:. The one NEG is the init
 * function's, for main's exit status. */
// listing-count: 1 NEG
// listing-lacks: BRNCH
// listing-lacks: ISEQ
// listing-lacks: ISNE
// listing-lacks: BRAND
// listing-lacks: BROR
// listing-count: 7 BRTRU
// listing-count: 2 BREQ
// listing-count: 1 BRNE

/* a > 5 && (b == 0 || c): LLW a CN 5 ISGT BRFLS F; LLW b BRFLS T;
 * LLW c BRFLS F; T: */
int g1(int a, int b, int c)
{
  int x = 0;
  if (a > 5 && (b == 0 || c))
    x = 1;
  return x;
}

/* a && b || c: LLW a BRFLS F; LLW b BRTRU T; F: LLW c BRFLS end; T: */
int g2(int a, int b, int c)
{
  int x = 0;
  if (a && b || c)
    x = 1;
  return x;
}

/* !(a && b): LLW a BRFLS F; LLW b BRTRU end; F: */
int g3(int a, int b)
{
  int x = 0;
  if (!(a && b))
    x = 1;
  return x;
}

/* a == b && a != 3: LLW a LLW b BRNE F; LLW a CN 3 BREQ F */
int g4(int a, int b)
{
  int x = 0;
  if (a == b && a != 3)
    x = 1;
  return x;
}

/* !(a || b): the || leaves its true label unplaced, and ! makes it the
 * false label: LLW a BRTRU G; LLW b BRTRU G */
int g5(int a, int b)
{
  int x = 0;
  if (!(a || b))
    x = 1;
  return x;
}

/* do ... while (x < a || x == b): both tests branch back to the top:
 * LLW x LLW a ISLT BRTRU top; LLW x LLW b BREQ top */
int g6(int a, int b)
{
  int x = 0;
  do
    x++;
  while (x < a || x == b);
  return x;
}

/* !(a && (b || c)): the && keeps the ||'s false label, so the BRNCH
 * that ! adds meets it: LLW a BRFLS F; LLW b BRTRU T; LLW c BRTRU T; F:
 * (T is the end) */
int g7(int a, int b, int c)
{
  int x = 0;
  if (!(a && (b || c)))
    x = 1;
  return x;
}

int main()
{
  __puti(g1(6, 0, 0)); __puti(g1(6, 1, 0)); __puti(g1(6, 1, 1));
  __puti(g1(5, 0, 1));
  __putln();
  __puti(g2(1, 1, 0)); __puti(g2(1, 0, 0)); __puti(g2(0, 1, 1));
  __puti(g2(0, 0, 0));
  __putln();
  __puti(g3(1, 1)); __puti(g3(1, 0)); __puti(g3(0, 1));
  __putln();
  __puti(g4(2, 2)); __puti(g4(3, 3)); __puti(g4(2, 3));
  __putln();
  __puti(g5(0, 0)); __puti(g5(1, 0)); __puti(g5(0, 2));
  __putln();
  __puti(g6(3, 9)); __puti(g6(2, 3)); __puti(g6(2, 2));
  __putln();
  __puti(g7(0, 1, 1)); __puti(g7(1, 0, 0)); __puti(g7(1, 1, 0));
  __puti(g7(1, 0, 1));
  __putln();
  return 0;
}
