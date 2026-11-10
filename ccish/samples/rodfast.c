// rodfast.c: Rod's Colors, the lores demo from the Apple II DOS System
// Master disk, in C for ccish, where speed is all that matters, at the
// cost of much more code. Press any key to exit. rod.c is the same
// program written plainly, calling CONIO to plot.
//
// The original (src/samplesrc/rod.bas and rod.pla) does, for each w
// from 3 to 50, forever:
//
//   for i = 1 to 19, for j = 0 to 19: k = i + j, color =
//     (3j / (i + 3) + i * w / 12) mod 16, and plot the 8 points
//     (i, k), (k, i), (40-i, 40-k), (40-k, 40-i),
//     (k, 40-i), (40-i, k), (i, 40-k) and (40-k, i).
//
// Call one pass over i, for one w, a frame. This version draws the
// same screen at the end of each frame, much faster, from these facts:
//
// 1. Each i plots rows i and 40-i, and columns i and 40-i, each at
//    positions t from lo(i) = min(i, 21-i) to 40-lo(i). Position t is
//    plotted by up to two j's, t-i and 40-i-t, and the later wins, so
//    each pixel's color in a frame comes from one (i, j): the largest i
//    that plots it, which we say owns it. Owners partition the screen.
// 2. So each i draws only the pixels it owns, once each: 1,520 pixels
//    a frame instead of 3,040 plots. Measured along a side from the
//    nearer edge, d = min(t, 40-t), i owns lo(i) <= d <= m(i), and the
//    middle, d = 20; past m(i), the columns of the i's after it win.
// 3. A color is q(j) + b, where q(j) = 3j / (i+3) is computed once, at
//    the start, and b = (i * w / 12) mod 16. When i's b is what it was
//    when i was last drawn, its pixels already have their colors, so
//    we skip it. For small i, b changes only every 12 / i frames, so
//    this skips about 35% of the pixels (986 a frame are drawn).
// 4. Rows 2r and 2r+1 share a byte. Down a column, both pixels of a
//    byte are the same i's, so we store whole bytes without reading
//    the screen. Along a row, a byte's other pixel is another i's, so
//    we read, mask and write, two bytes at once with 16-bit ops.
//
// The drawing has no multiply or divide (each is a 16-step loop in
// the VM). Within a frame, the screen differs from the original's
// only in the order its pixels change.
#include "lores.h"
#include "conio.h"

// The original's constants: the figure is SIZE by SIZE (0 to 40), and
// i runs from 1 to RINGS, j from 0 to J_MAX, and w from W_FIRST to
// W_LAST. A color is (Q_MUL * j) / (i + Q_ADD) + i * w / W_DIV.
constexpr int SIZE = 40;
constexpr int MID = SIZE / 2;
constexpr int RINGS = MID - 1;
constexpr int J_MAX = MID - 1;
constexpr int W_FIRST = 3;
constexpr int W_LAST = 50;
constexpr int W_DIV = 12;
constexpr int Q_MUL = 3;
constexpr int Q_ADD = 3;

// The figure's pixels, 0 to SIZE - 1 (no i plots 0 or SIZE), fit.
_Static_assert(SIZE <= LORES_WIDTH && SIZE <= LORES_HEIGHT, "fits");

// q(j) steps by at most 1 as j goes up (make_tables), since the
// smallest divisor, 1 + Q_ADD, is more than Q_MUL.
_Static_assert(1 + Q_ADD > Q_MUL, "q steps by at most 1");

// The original's text: grmode's mixed mode (4 lines of text below the
// graphics), and the message's place in those lines.
constexpr int MIXED = 1;
constexpr int MSG_X = 11;
constexpr int MSG_Y = 1;

// Never drawn: no b (0 to LORES_COLORS - 1) is this.
constexpr int NEVER = LORES_COLORS;

// The keyboard: KBD_READY is set when a key is waiting; reading the
// strobe clears it.
#define KBD     (*(volatile char *)0xC000)
#define KBDSTRB (*(volatile char *)0xC010)
constexpr int KBD_READY = 1 << 7;

// For each i and position t along a side: qlo[i][t] is i's q(j) at t,
// and qhi[i][t] is that in the high nibble, so a color is
// (qlo + b) & LORES_LO in an even row and
// (qhi + (b << LORES_HI_SHIFT)) & LORES_HI in an odd one.
static char qlo[RINGS + 1][SIZE + 1], qhi[RINGS + 1][SIZE + 1];

// qlo[i] and qhi[i], since indexing qlo by a variable i multiplies.
static char *qlo_of[RINGS + 1], *qhi_of[RINGS + 1];

// Each i's lo and m (fact 2), and the b it was last drawn with.
static char lo[RINGS + 1], hi_d[RINGS + 1], lastb[RINGS + 1];

// Compute the tables: each i's lo; its qlo and qhi, for each position t
// along a side of i's ring, the q that its color is q + b; and its m,
// where i's d stops, at the first column d of a later i reaching row i.
// The loop variables and limits are register, so no pointer can reach
// them and ccish can count the loops though they're nested and store
// through pointers (guide 12.2).
static void make_tables(void)
{
  char qj[J_MAX + 1], qjh[J_MAX + 1];
  register char *ql, *qh;
  register int i, j, t, d, q, qhv, r, l, e;

  for (i = 1; i <= RINGS; i++)
    lo[i] = i < SIZE - J_MAX - i ? i : SIZE - J_MAX - i;
  for (i = 1; i <= RINGS; i++) {
    // First, q for each j: the original's Q_MUL * j / (i + Q_ADD), in
    // qj[j], and the same in the high nibble, in qjh[j]. As j goes up
    // by 1, Q_MUL * j goes up by Q_MUL; keeping the quotient q and the
    // remainder r, the quotient goes up by 1 whenever the remainder
    // reaches the divisor d, and never by more, since d > Q_MUL. So
    // there is no divide (a 16-step loop in the VM), and qhv steps the
    // high-nibble copy along with q, so there is no shift either.
    d = i + Q_ADD;
    q = 0;
    qhv = 0;
    r = 0;
    for (j = 0; j <= J_MAX; j++) {
      qj[j] = q;
      qjh[j] = qhv;
      r += Q_MUL;
      if (r >= d) {
        r -= d;
        q++;
        qhv += 1 << LORES_HI_SHIFT;
      }
    }

    // Then q for each position t, from lo to SIZE - lo. The original
    // plots position t at j = t - i (as k = i + j) and at j = SIZE - i - t
    // (as SIZE - k), whichever of the two are in 0 to J_MAX, and the
    // later j is the one left on the screen. The two j's sum to
    // SIZE - 2i, so for t up to MID, SIZE - i - t is the larger: it wins
    // from e = SIZE - J_MAX - i up, where it comes into range; below e,
    // t - i is the only one. Past MID it's the same in mirror image (t
    // and SIZE - t have the same two j's), so those positions are
    // copies. Each loop steps j along with t, instead of computing it
    // from t on each pass. qlo_of[] and qhi_of[] keep the tables'
    // addresses, since main() would otherwise multiply to find qlo[i].
    ql = qlo[i];
    qh = qhi[i];
    qlo_of[i] = ql;
    qhi_of[i] = qh;
    l = lo[i];
    e = SIZE - J_MAX - i;
    j = l - i;                  // below e: j = t - i, going up; none
    for (t = l; t < e; t++) {   // for i >= 11, whose lo is e itself
      ql[t] = qj[j];
      qh[t] = qjh[j];
      j++;
    }
    j = SIZE - i - e;           // e to MID: j = SIZE - i - t, going down
    for (t = e; t <= MID; t++) {
      ql[t] = qj[j];
      qh[t] = qjh[j];
      j--;
    }
    e = SIZE - l;
    j = MID - 1;                // past MID: t's mirror, SIZE - t
    for (t = MID + 1; t <= e; t++) {
      ql[t] = ql[j];
      qh[t] = qh[j];
      j--;
    }

    // Last, m: d goes up from lo until a later i's column reaches row i.
    d = lo[i];
    while (d + 1 < MID && !(d + 1 > i && lo[d + 1] <= i))
      d++;
    hi_d[i] = d;
    lastb[i] = NEVER;
  }
}

// The drawing macros use main()'s locals: i and f = SIZE - i, the
// color offset b, b16 = b in the high nibble, b2 = the rows' b in both
// bytes of a word, i's tables ql, qh and qh1 = qh + 1, its rows' byte
// rows pi and pf, a byte row p, and the positions to draw, l to h.
// (Several small macros, since ccpp limits a macro's expansion to 256
// bytes.)

// Draw rows i and f from l to h, in nibble NIB (LO or HI), with the
// colors Q[x] + B: two bytes at a time, then the odd one out.
#define DRAW_ROWS(NIB, Q, B) \
  for (x = l; x < h; x += 2) { \
    s2 = (*(lores_word *)(Q + x) + b2) & LORES_##NIB##2; \
    LORES_SET_##NIB##2(pi + x, s2); \
    LORES_SET_##NIB##2(pf + x, s2); \
  } \
  DRAW_ROWS_END(NIB, Q, B)

// Draw the odd byte out at h, as DRAW_ROWS.
#define DRAW_ROWS_END(NIB, Q, B) \
  if (!((h - l) & 1)) { \
    s = (Q[h] + B) & LORES_##NIB; \
    LORES_SET_##NIB(pi + h, s); \
    LORES_SET_##NIB(pf + h, s); \
  }

// Draw the columns' ends: a row at an end whose byte it shares with a
// row outside [l, h] reads, masks and writes. p holds the byte row,
// so it's looked up once, not for each read and write.
#define DRAW_COL_ENDS() \
  if (l & 1) { \
    s = (qh[l] + b16) & LORES_HI; \
    p = lores_row[l]; \
    LORES_SET_HI(p + i, s); \
    LORES_SET_HI(p + f, s); \
  } \
  DRAW_COL_END()

// Draw the columns' end at h, as DRAW_COL_ENDS.
#define DRAW_COL_END() \
  if (!(h & 1)) { \
    s = (ql[h] + b) & LORES_LO; \
    p = lores_row[h]; \
    LORES_SET_LO(p + i, s); \
    LORES_SET_LO(p + f, s); \
  }

// Draw the rest of the columns, whole bytes, rows y and y + 1 from
// the first even y >= l.
#define DRAW_COLS() \
  for (y = (l + 1) & ~1; y < h; y += 2) { \
    p = lores_row[y]; \
    v = ((ql[y] + b) & LORES_LO) | ((qh1[y] + b16) & LORES_HI); \
    p[i] = v; \
    p[f] = v; \
  }

// i * w / W_DIV, from W_FIRST's quotient and remainder by W_DIV.
constexpr int WQ_FIRST = W_FIRST / W_DIV;
constexpr int WR_FIRST = W_FIRST % W_DIV;

// Run Rod's Colors until a key is pressed. For each i that has a new
// b, draw the pixels it owns with the color offset b (0 to 15): d from
// lo to m at both ends of each side, and the middle's four pixels, d =
// MID. Everything is register (no pointer reaches any of it), so ccish
// counts the drawing loops though they're nested and store through
// pointers.
int main(void)
{
  // i * w / W_DIV is q, with the remainder r: each i adds w, as wq and
  // wr (w's own quotient and remainder by W_DIV, stepped along with w),
  // so there's no divide. r stays under W_DIV, so one subtract is
  // enough after each add.
  register int w, wq, wr, i, q, r;
  register int x, y, l, h, m;
  register char b, f, s, v, b16;
  register lores_word s2, b2;
  register char *ql, *qh, *qh1, *pi, *pf, *p;

  conio.grmode(MIXED);
  conio.gotoxy(MSG_X, MSG_Y);
  __puts(pstr("Press any key to exit."));
  make_tables();
  for (;;) {
    wq = WQ_FIRST;
    wr = WR_FIRST;
    for (w = W_FIRST; w <= W_LAST; w++) {
      q = 0;
      r = 0;
      for (i = 1; i <= RINGS; i++) {
        q += wq;
        r += wr;
        if (r >= W_DIV) {
          r -= W_DIV;
          q++;
        }
        b = q & (LORES_COLORS - 1);
        if (b != lastb[i]) {
          lastb[i] = b;
          f = SIZE - i;
          b16 = b << LORES_HI_SHIFT;
          ql = qlo_of[i];
          qh = qhi_of[i];
          qh1 = qh + 1;
          pi = lores_row[i];
          pf = lores_row[f];
          // i and f have the same parity, so their rows the same nibble.
          if (i & 1)
            b2 = b16 | (b16 << LORES_BYTE_SHIFT);
          else
            b2 = b | (b << LORES_BYTE_SHIFT);
          m = hi_d[i];
          l = lo[i];
          h = m;
          if (i & 1) {
            DRAW_ROWS(HI, qh, b16)
          } else {
            DRAW_ROWS(LO, ql, b)
          }
          DRAW_COL_ENDS()
          DRAW_COLS()
          l = SIZE - m;
          h = SIZE - lo[i];
          if (i & 1) {
            DRAW_ROWS(HI, qh, b16)
          } else {
            DRAW_ROWS(LO, ql, b)
          }
          DRAW_COL_ENDS()
          DRAW_COLS()
          s = (ql[MID] + b) & LORES_LO;
          p = lores_row[MID];
          LORES_SET_LO(p + i, s);       // MID is even
          LORES_SET_LO(p + f, s);
          if (i & 1) {
            s = s << LORES_HI_SHIFT;
            LORES_SET_HI(pi + MID, s);
            LORES_SET_HI(pf + MID, s);
          } else {
            LORES_SET_LO(pi + MID, s);
            LORES_SET_LO(pf + MID, s);
          }
        }
        if (KBD & KBD_READY)
          goto done;
      }
      wr++;                     // the next w's quotient and remainder
      if (wr == W_DIV) {
        wr = 0;
        wq++;
      }
    }
  }
done:
  KBDSTRB;
  conio.textmode(0);
  __puts(pstr("That's all, folks!"));
  __putln();
  return 0;
}
