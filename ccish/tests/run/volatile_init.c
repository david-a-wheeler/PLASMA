/* A local that holds a volatile is initialized, and copied into, with
 * volatile stores, kept even though nothing reads them before the
 * function returns; a plain twin's stores go (peep.c, dead stores).
 * Two chars still pair into one word store, and zeros still go by word
 * or by memset: a frame can't be a device whose access width matters.
 *   vinit: a[4] = {1, 2}: CW 513 SLW, then 2 zeros, one SLW; w[2] =
 *     {5}: 2 SLWs; c[3] = {9}: the zeros' SLW, then the 9's SLB.
 *   vmember: a struct with a volatile member, initialized {1, 2}: both
 *     words volatile, 2 SLWs.
 *   vbig, pbig: an SLW and a memset each; the call takes the frame's
 *     address, so the plain twin keeps its store too.
 *   vcopy, pcopy: a struct copied from a global by memcpy.
 * pmember stores nothing; pinit keeps only its first SLW (the dead
 * stores pass misses it, as before volatile). So 1 SLB and 10 SLWs. vtake(gv)
 * pushes gv's words with volatile loads (push_struct_words), which
 * ccgen -d shows as LAW.v; the linked listing can't tell them from
 * plain LAWs, so this only checks that the call works. */
// listing-count: 1 SLB
// listing-count: 10 SLW
// listing-count: 2 CALL $0000  ; __memset
// listing-count: 2 CALL $0000  ; __memcpy
struct vs { volatile int a; int b; };
struct ps { int a; int b; };
struct vs gv = {7, 8};
struct ps gp = {7, 8};

void vinit(void)
{
  volatile char a[4] = {1, 2};
  volatile int w[2] = {5};
  volatile char c[3] = {9};
}

void vbig(void)
{
  volatile int big[8] = {1};
}

void pinit(void)
{
  char a[4] = {1, 2};
  int w[2] = {5};
  char c[3] = {9};
}

void pbig(void)
{
  int big[8] = {1};
}

void vmember(void)
{
  struct vs x = {1, 2};
}

void pmember(void)
{
  struct ps x = {1, 2};
}

void vcopy(void)
{
  struct vs x;
  x = gv;
}

void pcopy(void)
{
  struct ps x;
  x = gp;
}

int vtake(struct vs s)
{
  return s.a;
}

int ptake(struct ps s)
{
  return s.a;
}

int main()
{
  vinit();
  pinit();
  vbig();
  pbig();
  vmember();
  pmember();
  vcopy();
  pcopy();
  __puti(gv.a + gp.b + vtake(gv) + ptake(gp));
  __putln();
  return 0;
}
