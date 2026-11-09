// genhoist.c - hoisting (irloop.c, hoist()) from loop bodies with and
// without an IR_SWAP. The hoisted expression's place keeps its length,
// a load of its temporary and then padding (CONST 0 DROP pairs, and
// CONST 0 NEG DROP for an odd count), which the peephole stage removes:
// no CN 0 DROP is left below. A body with an IR_SWAP may exchange at
// run time through FRAME_TEMP(0) and (1), so its hoisted values take
// (2) and (3), shown as -4 and -5; others take -2 and -3.
char a[12];
int b[8] = {10, 20, 30, 40, 50, 60, 70, 80};

// a[i + 1] = ... is an IR_SWAP (value, then address, exchanged), and
// x * y / 3 (5 ops: 4 padded, two pairs) is in its value block: hoisted
// to SLW -4, loaded as LLW -4, the block's length kept.
int with_swap(int x, int y)
{
  int i;
  for (i = 0; i < 10; i++)
    a[i + 1] = x * y / 3 + i;
  return a[5];
}

// (x + 1) * y is 6 ops: 5 padded, CONST 0 NEG DROP and one pair.
// No IR_SWAP: SLW -2.
int odd(int x, int y)
{
  int i, s = 0;
  for (i = 0; i < 10; i++)
    s += (x + 1) * y + i;
  return s;
}

// Two in one body: SLW -2 and SLW -3.
int two(int x, int y)
{
  int i, s = 0;
  for (i = 0; i < 4; i++)
    s += x * y / 3 + x * 5 / y;
  return s;
}

// *q /= g(i) exchanges at run time (its g(i) must stay before *q's
// load: IR_SWAP 2 -1), stashing through FRAME_TEMP(0) (SLW -2, LLW -2)
// in every pass, while x * y / 3 is hoisted to -4: were it in -2, the
// stash would overwrite it.
int g(int v) { return v + 1; }
int stash(int x, int y)
{
  int i, s = 0, *q;
  for (i = 0; i < 5; i++) {
    q = &b[i];
    *q /= g(i);
    s += x * y / 3 + b[i];
  }
  return s;
}

// with_swap(4, 6): a[5] is 8 + 4, 12; odd(2, 5): 10 * 15 + 45, 195;
// two(6, 3): 4 * (6 + 10), 64; stash(4, 6): b[i] all become 10, so 5
// * (8 + 10), 90.
int main(void)
{
  return with_swap(4, 6) == 12 && odd(2, 5) == 195 && two(6, 3) == 64
         && stash(4, 6) == 90;
}
