// unsigned int (approach.md, "unsigned and helper functions design"):
// the type in every spelling, C's usual arithmetic conversions (int
// meeting unsigned becomes unsigned), unsigned comparisons, >> and /
// and % by constants done inline (no DIV or MOD), constants (a u
// suffix; hex over $7FFF), sizeof as unsigned, and constant folding
// with unsigned rules. Values over 32767 are printed in hex (puth).
// listing-lacks: DIV
// listing-lacks: MOD
void show(int n)
{
  __cmd_puti(n);
  __cmd_putc(' ');
}

void showh(unsigned n)
{
  __cmd_puth(n);
  __cmd_putc(' ');
}

unsigned half(unsigned x)
{
  return x >> 1;
}

int main()
{
  unsigned u = 40000u, v = 3;
  unsigned int w = 0xFFFF;
  int unsigned x = 0x8000;
  const unsigned k = 16;
  unsigned char c = 200;
  int n = -1;

  // comparisons are unsigned when either side is
  show(u > 1); show(u < 1); show(n < 1u); show(n < 1); show(0x8000 > 0);
  show(100 < u); show(u > v); show(c > 100);
  __cmd_putln();
  // >>, / and % by constants
  showh(u >> 4); showh(w >> 15); show((int)u >> 4); showh(u / 8);
  showh(w / 2); showh(u % 8); showh(40001u % 8); showh(w % 16);
  showh(x >> 15);
  __cmd_putln();
  // constants fold with unsigned rules
  show(0xFFFFu / 2 == 32767); show(0x8000 >> 15); show(-1 >> 15);
  show(sizeof(int) - 3 > 0); show((unsigned)-1 > 0); show(-1 > 0);
  __cmd_putln();
  // compound assignment, a call, ?:, << and ~
  u >>= 1; showh(u);
  u /= 4; showh(u);
  u %= 32; showh(u);
  showh(half(w)); showh(n < 0 ? w : 1u); showh(k << 11); showh(~0u >> 1);
  __cmd_putln();
  show(sizeof(unsigned)); show(sizeof(unsigned char));
  __cmd_putln();
  return 0;
}
