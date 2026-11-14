// A loop driven purely by goto (no for or while): the backward goto
// ('top') keeps it going and the forward goto ('done') leaves it. It
// sums 5 down to 1. A goto loop is never counted, so the index rides
// in the frame and the test is a real comparison.
int main(void)
{
  int i = 5, sum = 0;
top:
  if (i < 1)
    goto done;
  sum += i;
  i = i - 1;
  goto top;
done:
  __cmd_puti(sum);
  __cmd_putln();
  return 0;
}
