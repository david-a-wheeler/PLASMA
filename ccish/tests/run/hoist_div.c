// hoist_div.c: loop-invariant hoisting (irloop.c, hoist()): a costly
// op in a loop body, whose whole expression changes no part of the
// loop (a division by a nonzero constant, here; also a modulo, an
// unsigned arithmetic, a multiply by a hard constant), is moved
// before the loop, its value stored in a frame temporary the body
// loads. The body keeps its length, so the vacated ops are padded
// with CONST 0, DROP pairs, and with one CONST 0, NEG, DROP when the
// count is odd. The padding is removed by the peephole stage; if it
// were wrong in length or value, the loop's arithmetic would run on a
// corrupted stack, and if the hoisting dropped a sign or mixed up the
// temporary, the sums below would not be n times the per-pass value.
void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

// -x / 7 is four ops, x NEG CONST 7 DIV: three, an odd number, before
// the division, so the padding is the odd one.
int divs(int x, int n)
{
  int i, s = 0;
  for (i = 0; i < n; i++)
    s += -x / 7;
  return s;
}

// The modulo version of the same shape.
int mods(int x, int n)
{
  int i, s = 0;
  for (i = 0; i < n; i++)
    s += -x % 7;
  return s;
}

// x / 7 is three ops, x CONST 7 DIV: two, an even number, before the
// division, so the padding is the even one.
int divv(int x, int n)
{
  int i, s = 0;
  for (i = 0; i < n; i++)
    s += x / 7;
  return s;
}

int main(void)
{
  show(divs(14, 5));        // 5 * (-14 / 7) = -10
  show(divs(-21, 4));       // 4 * (21 / 7) = 12
  show(mods(14, 5));        // -14 % 7 is 0
  show(mods(15, 5));        // 5 * (-15 % 7) = -5
  show(divv(14, 5));        // 5 * (14 / 7) = 10
  show(divv(15, 5));        // 5 * (15 / 7) = 10
  __cmd_putln();
  return 0;
}
