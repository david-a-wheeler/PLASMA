// genvolprobe.c - every optimization must leave volatile accesses as
// the program wrote them (approach.md issue 25), with plain parts still
// optimized: these are the cases most likely to slip when a rule is
// added. Each comment says what must stay. genvolatile.c has the soft
// switches and twins.
volatile int vg;
volatile unsigned vu;
volatile char va[4];
struct s { int a; int b; };
volatile struct s vs;
struct m { volatile int a; int b; };
struct m ms;
volatile int *vp;
int g;

// An element of a volatile array, as a statement: read.
int elem(void) { va[2]; return 0; }

// Members: of a volatile struct, and a volatile member, read; a plain
// member of the same struct, not.
int member(void) { vs.b; ms.a; ms.b; return 0; }

// x * 0 and x & 0 are 0, x - x 0, x + 0 x: vg is read each time.
int times0(void) { return vg * 0; }
int and0(void) { return (vg & 0) + (vg - vg); }
int plus0(void) { return vg + 0; }

// A cast to void reads.
int tovoid(void) { (void)vg; return 0; }

// An empty if still tests (reads) vg.
int emptyif(void) { if (vg) {} return 0; }

// Equal arms: vg is still read, then dropped (irin.c, run_do).
int samearms(void) { return vg ? 1 : 1; }

// An unsigned >= 0 is always 1, but vu is read.
int uge0(void) { return vu >= 0; }

// !!vg, and vg == vg: one read, then two.
int notnot(void) { return !!vg; }
int selfeq(void) { return vg == vg; }

// vg = vg: one read and one store.
int selfset(void) { vg = vg; return 0; }

// A volatile char local: its initializer stored, and the range test
// reads it twice (no betweenness, irin.c).
int vrange(void) { volatile char c = 50; return c >= 48 && c <= 55; }

// A limit read through a volatile pointer: read each pass, so the loop
// isn't counted (irloop.c, limit_ok).
int vlimit(void) { int i, n = 0; for (i = 0; i < *vp; i++) n++; return n; }

// A volatile init: read once; the loop is still counted.
int vinit(void) { int i, n = 0; for (i = vg; i < 10; i++) n++; return n; }

// A dead store of a volatile's value: the store goes, the read stays.
int deadstore(void) { int x = vg; x = 1; return x; }

// A counted loop's body: a read, and a store, each pass.
int vbody(void) { int i; for (i = 0; i < 3; i++) vg; return 0; }
int vbodyset(void) { int i; for (i = 0; i < 3; i++) vg = i; return 0; }

// sizeof doesn't evaluate: no read.
int size(void) { return sizeof(vg); }

// Unsigned / % >> by constants: each reads vu.
int ushift(void) { return vu / 4 + vu % 4 + (vu >> 1); }

// A plain global stored twice from vg: two reads, two stores.
int twice(void) { g = vg; g = vg; return 0; }

// A wait loop: vg reloaded each pass.
int vwait(void) { while (vg) ; return 0; }

// ++ and --: one read and one store each.
int bumps(void) { vg++; ++vg; vg--; return 0; }

// Each in turn, so vg's changes come in order; the status says which
// failed (gcc agrees). vg is 0 until vbodyset leaves it 2.
int main(void)
{
  vp = &g;
  vwait();
  if (elem() || member() || times0() || and0() || plus0() || tovoid()
      || emptyif() || samearms() != 1 || uge0() != 1 || notnot()
      || selfeq() != 1 || selfset())
    return 1;
  if (vrange() != 1 || vlimit() || vinit() != 10 || deadstore() != 1
      || vbody() || size() != sizeof(int) || ushift() || twice())
    return 2;
  vbodyset();
  bumps();
  return vg != 3;               // 2, then +1 +1 -1
}
