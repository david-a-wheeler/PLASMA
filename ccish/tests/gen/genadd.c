/* genadd.c - adding a constant or a local: a constant that fits
 * becomes INCR, DECR, ADDI or SUBI; adding zero adds nothing; and a
 * local that is added (or twice added) fuses into the load. */
int add3(int x)    { return x + 3; }
int sub5(int x)    { return x - 5; }
int plus1(int x)   { return x + 1; }
int minus1(int x)  { return x - 1; }
int add255(int x)  { return x + 255; }
int zero(int x)    { return x + 0; }
int sum(int x, int y)  { return x + y; }
int dbl(int x, int y)  { return x + 2 * y; }

int main(void)
{
  add3(1);
  sub5(1);
  plus1(1);
  minus1(1);
  add255(1);
  zero(1);
  sum(1, 1);
  dbl(1, 1);
  return 0;
}
