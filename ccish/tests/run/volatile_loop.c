/* A counted loop computes its limit once, at its entry (irloop.c), so
 * a volatile limit makes the loop an ordinary one, which reads it each
 * pass; the plain twin is counted, its bottom one BRLT. The globals: vn
 * data+0, pn data+2. A loop over a volatile local isn't counted either
 * (step_shape() and the DLW fix name the plain ops): only ploop's
 * i < 4 is, its bottom one INCBRLE. */
// listing-count: 1 BRLT
// listing-count: 1 INCBRLE
volatile int vn = 5;
int pn = 5;

int vsum(void)
{
  int i, s = 0;
  for (i = 0; i < vn; i++)
    s += i;
  return s;
}

int psum(void)
{
  int i, s = 0;
  for (i = 0; i < pn; i++)
    s += i;
  return s;
}

int vloop(void)
{
  volatile int i;
  int s = 0;
  for (i = 0; i < 4; i++)
    s += i;
  return s;
}

int ploop(void)
{
  int i, s = 0;
  for (i = 0; i < 4; i++)
    s += i;
  return s;
}

int main()
{
  __puti(vsum());
  __putc(' ');
  __puti(psum());
  __putc(' ');
  __puti(vloop());
  __putc(' ');
  __puti(ploop());
  __putln();
  return 0;
}
