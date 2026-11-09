// genbetweenglobal.c - a betweenness test on a global (not volatile)
// is one load, as on a local: x >= c1 && x <= c2 over a power-of-two
// block becomes a mask and one compare, for an int global, a char
// global and an unaligned block. A volatile global keeps both loads.
// main checks each at the block's edges and just outside, and returns
// 0 only if every answer is right.
int g;
char c;
volatile int vg;

int in4(void)     { return g >= 300 && g <= 303; }
int in64(void)    { return c >= 64 && c <= 127; }
int in2odd(void)  { return g >= 301 && g <= 302; }
int vin4(void)    { return vg >= 300 && vg <= 303; }

int main(void)
{
  int bad = 0;
  g = 299; bad += in4();
  g = 300; bad += !in4();
  g = 303; bad += !in4();
  g = 304; bad += in4();
  c = 63;  bad += in64();
  c = 64;  bad += !in64();
  c = 127; bad += !in64();
  c = 128; bad += in64();
  g = 300; bad += in2odd();
  g = 301; bad += !in2odd();
  g = 302; bad += !in2odd();
  g = 303; bad += in2odd();
  vg = 301; bad += !vin4();
  vg = 304; bad += vin4();
  return bad;
}
