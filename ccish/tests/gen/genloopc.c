// genloopc.c - a char loop variable in a counted loop (irloop.c,
// char_count_ok()): the count is a word on the eval stack, which
// doesn't wrap at 256 as the char does, so a char loop is counted only
// when the count stays in 0 to 255 to the end: a constant limit with
// room for the last step, or a char local limit with < or > by 1. The
// top of each pass stores the char with DLB. A char loop that calls
// may end at != (genne.c).
int n;

// Count n up by x.
void tick(int x) { n += x; }

// Counted: CB 200 ... INCBRLE (the limit 199), DLB at the top.
int up_lt(void) { char c; int s = 0; for (c = 0; c < 200; c++) s += c; return s; }

// Counted: c <= 254 ends at 255, which fits.
int up_le(void) { char c; int s = 0; for (c = 250; c <= 254; c++) s += c; return s; }

// Counted, down: c > 0 ends at 0; c >= 1 at 0 too.
int down_gt(void) { char c; int s = 0; for (c = 5; c > 0; c--) s += c; return s; }
int down_ge(void) { char c; int s = 0; for (c = 3; c >= 1; c--) s += c; return s; }

// Counted: a char limit, < by 1.
int by_char(char m) { char c; int s = 0; for (c = 0; c < m; c++) s += c; return s; }

// Counted: by 5 to under 250, the last count 250.
int by5(void) { char c; int s = 0; for (c = 0; c < 250; c += 5) s += c; return s; }

// Not: c <= 255 never ends in C (the break ends it here); the count
// would.
int le255(void)
{
  char c;
  int s = 0;
  for (c = 250; c <= 255; c++) {
    s += c;
    if (c == 255)
      break;
  }
  return s;
}

// Not: c >= 0 is always so.
int ge0(void)
{
  char c;
  int s = 0;
  for (c = 3; c >= 0; c--) {
    s += c;
    if (c == 0)
      break;
  }
  return s;
}

// Not: c += 3 from 253 wraps to 0 in C, so it goes on: 87 passes, where
// a count would make 2.
int wraps(void) { char c; int s = 0; for (c = 250; c < 255; c += 3) s++; return s; }

// Not: c <= m might be c <= 255.
int le_char(char m) { char c; int s = 0; for (c = 0; c <= m; c++) s += c; return s; }

// Not: an int limit might be past 255.
int by_int(int m) { char c; int s = 0; for (c = 0; c < m; c++) s += c; return s; }

// Not counted (a call), but ends CN 5 BRNE.
void calls(void) { char c; for (c = 0; c < 5; c++) tick(c); }

// The sums: 0 to 199 is 19900; 250 to 254, 1260; 5 to 1, 15; 3 to 1,
// 6; 0 to 3, 6; 0, 5, ... 245, 6125; 250 to 255, 1515; 3 to 0, 6; 87
// passes; 0 to 3, 6; 0 to 3, 6; and n, 0 to 4, 10.
int main(void)
{
  calls();
  return up_lt() == 19900 && up_le() == 1260 && down_gt() == 15
         && down_ge() == 6 && by_char(4) == 6 && by5() == 6125
         && le255() == 1515 && ge0() == 6 && wraps() == 87
         && le_char(3) == 6 && by_int(4) == 6 && n == 10;
}
