/* gendead.c - the dead code: after a return (or a jump), nothing runs
 * until the next label. The jump after an if-part that ends in a
 * return, and the implicit return the parser adds after a final return
 * statement, are dropped. The labels stay: they are where a branch can
 * land, which makes the code reachable again. */
int pick(int x)
{
  if (x)
    return 1;
  return 2;
}

int main(void)
{
  return pick(0) == 2;
}
