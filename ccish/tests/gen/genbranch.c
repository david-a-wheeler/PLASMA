/* genbranch.c - the short-circuit and or: each side branches to the
 * common false label, and the jump that would go around the false part
 * meets that label in the peephole stage, where it becomes the
 * opposite test (the second BRFALSE and the BRNCH become one BRTRU). */
int both(int a, int b)
{
  return a && b;
}

int either(int a, int b)
{
  return a || b;
}

int neither(int a, int b)
{
  return !(a && b);
}

int main(void)
{
  return both(1, 0) + either(0, 1) + neither(0, 1) == 2;
}
