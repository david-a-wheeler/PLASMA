/* Tail depth: a tail call reuses the function's frame (its jump goes
 * to the label just after the ENTER, storing the args into the
 * current frame's slots), so a tail recursion runs at any depth,
 * while a non-tail recursion dies on the plvm stack at a few hundred
 * frames. The deep sum wraps 16 bits: 5000 * 5001 / 2 is 12502500,
 * 50660 in the low 16 bits, which puti prints signed. */
int tdeep(int n, int acc)
{
  if (n <= 0)
    return acc;
  return tdeep(n - 1, acc + n);
}
int nshallow(int n)
{
  if (n <= 0)
    return 0;
  return nshallow(n - 1) + 1;
}
int main(void)
{
  __puti(nshallow(100)); __putln();
  __puti(tdeep(5000, 0)); __putln();
  return 0;
}
