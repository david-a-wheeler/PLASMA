// Truth runs (approach.md, decision 31): an if whose arms return 1
// and 0 is the condition's truth, so it compiles to the condition's
// value, with no branch or label at all. The while keeps its test
// branch. The init function adds an ANDI and a NEG, for main's exit
// status. is_zero's n == 0 is NOT (CN 0 ISEQ, peep.c), as is_neg's !
// is the other.
// listing-lacks: ISEQ
// listing-count: 1 ISNE
// listing-count: 2 NOT
// listing-count: 2 ANDI
// listing-count: 3 NEG
// listing-count: 1 ISLT
// listing-count: 1 BRTRU
// listing-count: 1 BRNCH
// listing-lacks: BREQ
// listing-lacks: BRNE
// listing-lacks: BRFLS
void show(int n)
{
  __puti(n);
  __putc(' ');
}

int is_zero(int x)
{
  if (x == 0)
    return 1;
  return 0;
}

int differ(int x, int y)
{
  if (x != y)
    return 1;
  return 0;
}

int is_not(int x)
{
  if (!x)
    return 1;
  return 0;
}

int up_to(int x, int y)
{
  while (x < y)
    x++;
  return x;
}

int main()
{
  show(is_zero(0)); show(is_zero(3)); show(differ(1, 2));
  show(differ(2, 2)); show(is_not(0)); show(is_not(5)); show(up_to(1, 4));
  __putln();
  return 0;
}
