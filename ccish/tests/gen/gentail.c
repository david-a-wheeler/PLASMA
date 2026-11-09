// gentail.c - simple tail recursion (approach.md, decision 33): a
// function's own tail call, whose result is returned directly,
// becomes the args' stores into the parameter slots, the counted
// loop's values' drops, and a jump to the function's entry label
// (the label after its ENTER, 103), instead of a call: no new frame
// per recursion. A self call whose result is used, that is not
// followed by the return, or a call to another function, stays a
// call.
int gt0(int n)
{
  if (n <= 0)
    return 0;
  return gt0(n - 1);
}
void gt1(int n, int *p)
{
  if (n <= 0)
    return;
  *p = n;
  gt1(n - 1, p);
}
void gt6(int n)
{
  gt6(n - 1);   // no base case: the tail jump is the loop
}
int gt2(int n)
{
  int i;
  int s = 0;
  for (i = 0; i < n; i++)
  {
    if (i == 3)
      return gt2(i);
    s += i;
  }
  return s;
}
int gt3(int n)
{
  int i = 0;
  while (i < n && i != 7)
  {
    if (n - i < 2)
      return gt3(n - i - 1);
    i++;
  }
  return i;
}
int gt4(int n)
{
  if (n <= 0)
    return 1;
  return n * gt4(n - 1);
}
int gt5(int n)
{
  if (n <= 0)
    return 0;
  gt5(n - 1);
  return n;
}
int main(void)
{
  int x;
  gt0(3);
  gt1(3, &x);
  gt2(5);
  gt2(2);
  gt3(9);
  gt3(1);
  gt4(3);
  gt5(2);
  return 0;
}
