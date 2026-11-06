/* genandi.c - consecutive byte-ANDs fold into one: ANDI m ANDI n
 * becomes ANDI (m & n). A (char) cast on an int emits ANDI 255 (clear
 * the high byte) and a small & constant emits ANDI n; the pair merges
 * to ANDI (255 & n), which is just ANDI n, and a third mask merges
 * again. A char operand needs no cast, so it has no pair to fold. */
int foldpair(int x)
{
  return (int)((char)(x) & 240);
}

int foldthree(int x)
{
  return (int)((char)(x) & 240 & 192);
}

int nofold(char c)
{
  return c & 240;
}

int main(void)
{
  foldpair(300);
  foldthree(500);
  nofold('a');
  return 0;
}
