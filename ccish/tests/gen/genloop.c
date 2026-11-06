/* genloop.c - the counted for-loop: the limit and the count ride on
 * the stack for the life of the loop (the exit drops both), the test
 * is the counted branch (BRLT up, BRGT down), the step is a plain
 * ADDI or SUBI, and the index's per-turn store keeps the count for
 * the body. The PLASMA-for fusions (INCBRLE, ...) are not reachable
 * from C's for. */
int total(int n)
{
  int i, s;
  s = 0;
  for (i = 0; i < n; i++)
    s += i;
  return s;
}

int up3(int n)
{
  int i, s;
  s = 0;
  for (i = 0; i < n; i += 3)
    s += i;
  return s;
}

int down2(int n)
{
  int i, s;
  s = 0;
  for (i = n; i > 0; i -= 2)
    s += i;
  return s;
}

int main(void)
{
  up3(9);
  down2(9);
  return total(4) == 6;
}
