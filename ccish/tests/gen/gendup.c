// gendup.c - a load repeated (peep.c, repeats_load()) is a DUP: the
// same local or global loaded twice in a row, or loaded right after a
// keeping store of it. Not a volatile, read each time; and not when
// the second load fuses with what follows (x + x is LLW ADDLW).
int g;
volatile int vg;

// LLW 0 DUP MUL.
int sq(int x) { return x * x; }

// LLB 0 DUP MUL (a char parameter's byte).
int sqc(char c) { return c * c; }

// LAW DUP MUL, for a global.
int sqg(void) { return g * g; }

// y stored and kept (DLW 2), then y again (DUP); then nothing reads y,
// so the store is dead before the LEAVE: LLW 0 INCR DUP MUL.
int sq1(int x) { int y; y = x + 1; return y * y; }

// LLW 0 ADDLW 0: the fused add, not DUP ADD.
int dbl(int x) { return x + x; }

// LAW.v LAW.v MUL: a volatile is read twice.
int sqv(void) { return vg * vg; }

// 9, 16, 4, 25 and 6 make 60; vg is 0.
int main(void)
{
  g = 2;
  return sq(3) + sqc(4) + sqg() + sq1(4) + dbl(3) + sqv() == 60;
}
