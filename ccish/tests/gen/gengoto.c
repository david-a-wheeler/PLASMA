// gengoto.c - a goto and its label: the label takes a goto label (the
// block above the entry label, 104 up), and the goto is an ordinary
// branch to it (pick). A counted loop keeps its limit and count under
// its body, so a for-loop whose body holds a goto is kept uncounted:
// sum's loop is the plain form, its index in the frame and its test a
// real comparison, not the counted form's stack index and BRLT.
int pick(int n)
{
  if (n)
    goto yes;
  return 0;
yes:
  return 1;
}

int sum(int n)
{
  int i, s;
  s = 0;
  for (i = 0; i < n; i++) {
    if (i == 3)
      goto out;
    s += i;
  }
  out:
  return s;
}

int main(void)
{
  return pick(1) + sum(5) == 4;
}
