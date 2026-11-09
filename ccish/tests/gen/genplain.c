// genplain.c - plain (not volatile) loads have no effect a C program
// may see, so they go when their value does (approach.md issue 25):
// I/O must be volatile (genvolatile.c has the twins). KBD is the
// Apple II keyboard, deliberately declared without volatile: a program
// that does this is wrong, and its reads may go. Under plvm it's
// ordinary memory.
char *const KBD = (char *)0xC000;
int g, h;
int a[4];
int *p;

// Loads as statements: nothing.
int stmts(int i)
{
  *p;
  a[i];
  *KBD;
  return 0;
}

// A dead store's value: a load through a pointer goes with it.
int deadload(void)
{
  int x = *p;
  int y = a[2] + g;
  return 0;
}

// Values computed and dropped: a comparison, a division.
int dropped(void)
{
  (void)(g < h);
  (void)(g / h);
  return 0;
}

// A limit or an init read through a plain I/O address: the loop is
// still counted, its limit read once and kept on the stack (irloop.c);
// the loop variable is never loaded to test it.
int limit(void)
{
  int i, n = 0;
  for (i = 0; i < *KBD; i++)
    n++;
  return n;
}

int init(void)
{
  int i, n = 0;
  for (i = *KBD; i < 10; i++)
    n++;
  return n;
}

// A do-nothing delay loop: C lets a compiler remove it, since nothing
// it does can be seen, and ccish does (irloop.c), leaving i its last
// value, a store dead_stores() then drops. A delay needs a volatile
// counter, whose every store happens.
void delay(void)
{
  int i;
  for (i = 0; i < 500; i++)
    ;
}

void vdelay(void)
{
  volatile int i;
  for (i = 0; i < 500; i++)
    ;
}

int main(void)
{
  p = &g;
  delay();
  vdelay();
  return stmts(1) + deadload() + dropped() + limit() + init() - 10;
}
