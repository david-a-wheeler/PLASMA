/* genstore.c - the stores: an assignment statement keeps no copy of
 * its value (DLW and DROP become SLW), a store followed by a load of
 * the same local keeps the value for the load (SLW and LLW become
 * DLW), a store to a local that nothing reads again is not made, a
 * store through a pointer is one SW of the value and the address (it
 * may write anywhere, so it is not touched), the x++ whose old value
 * is unused drops the DUP and the DROP, and a pure expression
 * statement drops with everything it read. */
int keep(int x)
{
  int y;
  y = x;
  return y;
}

int drop(void)
{
  int y;
  y = 5;
  return 3;
}

int set(int *p)
{
  *p = 7;
  return 0;
}

int bump(int x)
{
  x++;
  return x;
}

int use2(int x, int y)
{
  (x + y);       /* a pure expression as a statement: nothing runs */
  return 0;
}

int main(void)
{
  int a;
  a = 1;
  keep(1);
  drop();
  set(&a);
  bump(1);
  use2(1, 2);
  return a;
}
