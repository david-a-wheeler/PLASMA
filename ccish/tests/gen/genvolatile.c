// genvolatile.c - volatile accesses happen just as the program says
// (approach.md issue 25), each beside a plain twin that's still
// optimized. The Apple II's soft switches are volatile chars at
// constant addresses: SPEAKER ($C030) and KEYBOARD ($C000) are read,
// KBDSTRB ($C010) is written (any store clears the key strobe). A
// const pointer's known value folds, for a store too, so each is one
// absolute op (LAB.v, SAB.v). Under plvm they're ordinary memory.
volatile char *const SPEAKER = (volatile char *)0xC030;
volatile char *const KEYBOARD = (volatile char *)0xC000;
volatile char *const KBDSTRB = (volatile char *)0xC010;
volatile int vg;
int g;
char gc;

// Two clicks: two reads, each kept though its value isn't used.
void click(void)
{
  *SPEAKER;
  *SPEAKER;
}

// The plain twin: the reads go.
void touch(void)
{
  gc;
  gc;
}

// Two stores, both kept, with no copy of the value kept.
void clear(void)
{
  *KBDSTRB = 0;
  *KBDSTRB = 0;
}

// A store through a volatile pointer variable: cccomp keeps a copy of
// the value (DUP), as it can't know the statement drops it (DROP);
// ccgen removes the pair (peep.c, drop_dup). Indexed too.
void poke(volatile char *p, char v)
{
  *p = v;
}

void pokew(volatile int *p, int i)
{
  p[i] = 5;
}

// A counted loop: the read stays in the body.
void tone(int n)
{
  int i;
  for (i = 0; i < n; i++)
    *SPEAKER;
}

// The plain twin: the body is empty.
void spin(int n)
{
  int i;
  for (i = 0; i < n; i++)
    gc;
}

// Is an octal digit's key down (bit 7 set)? KEYBOARD is read twice,
// once per comparison.
int voctal(void)
{
  return *KEYBOARD >= 0x80 + '0' && *KEYBOARD <= 0x80 + '7';
}

// The plain twin, on a parameter: one load and one compare
// (genbetween.c), since '0' to '7' is an aligned block of 8.
int octal(char c)
{
  return c >= '0' && c <= '7';
}

// A store then a load: both happen.
int vstore(void)
{
  vg = 3;
  return vg;
}

// The plain twin: the load reuses the stored value (DAW).
int store(void)
{
  g = 3;
  return g;
}

// Compound assignment and ++: one load and one store each, and the
// value of vg++ is the one loaded, not a read back.
int vbump(void)
{
  vg += 2;
  return vg++;
}

// A local's stores are kept though nothing reads them.
int vlocal(void)
{
  volatile int t = 5;
  t = 6;
  return 1;
}

// The plain twin: the dead stores go.
int local(void)
{
  int t = 5;
  t = 6;
  return 1;
}

// A loop's test reloads vg each pass.
int vwait(void)
{
  int n = 0;
  while (vg)
    n++;
  return n;
}

int main(void)
{
  char c = 0;
  click();
  touch();
  clear();
  poke(&c, 7);
  pokew(&g, 0);
  tone(3);
  spin(3);
  voctal();
  if (c != 7 || !octal('5') || octal('8'))
    return 1;
  if (g != 5)
    return 3;
  if (vstore() != 3 || store() != 3 || vbump() != 5 || vg != 6)
    return 2;
  vg = 0;
  return vlocal() + local() + vwait() - 2;
}
