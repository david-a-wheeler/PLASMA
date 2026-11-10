/* lores.h: Apple II low-resolution graphics (40 by 48, 16 colors) for
 * ccish programs. Goes with lores.c, which defines lores_row[].
 *
 * Lores row y lives in the low nibble of a screen byte when y is even,
 * and in the high nibble when y is odd; rows 2r and 2r+1 share text
 * row r's bytes. lores_row[y] is the address of the byte row holding
 * row y, so pixel (x, y) is in lores_row[y][x]. The plot macros are
 * macros, not functions, so a hot loop that uses them makes no calls
 * (a call costs about 80 cycles and keeps a loop from being counted,
 * guide section 12.2). Each macro evaluates its arguments more than
 * once: pass plain variables, not expressions with side effects.
 *
 * The ..2 macros work on two bytes at once, pixels (x, y) and
 * (x + 1, y), with one 16-bit load and store: about half the VM ops
 * of two single plots. */
#ifndef LORES_H
#define LORES_H

constexpr int LORES_TEXT_ROWS = 24;
constexpr int LORES_WIDTH = 40;
constexpr int LORES_HEIGHT = 2 * LORES_TEXT_ROWS;  /* 2 pixels a byte */

/* The nibbles: the shift from even rows' (low) to odd rows' (high),
 * the colors, and their masks, alone and for two adjacent bytes. */
constexpr int LORES_HI_SHIFT = 4;
constexpr int LORES_COLORS = 1 << LORES_HI_SHIFT;
constexpr int LORES_LO = LORES_COLORS - 1;
constexpr int LORES_HI = LORES_LO << LORES_HI_SHIFT;
constexpr int LORES_BYTE_SHIFT = 8;
constexpr unsigned LORES_BYTE2 = 1 + (1u << LORES_BYTE_SHIFT);
constexpr unsigned LORES_LO2 = LORES_LO * LORES_BYTE2;
constexpr unsigned LORES_HI2 = LORES_HI * LORES_BYTE2;

/* Two adjacent screen bytes, read or written at once (16 bits under
 * ccish and gcc both). */
#ifdef __CCISH__
typedef unsigned lores_word;
#else
typedef unsigned short lores_word;
#endif

/* The byte rows (each row y's byte row, so pairs repeat). Rows 40-47
 * are the four text lines of the mixed-mode screen. */
extern char *const lores_row[LORES_HEIGHT];

/* Set the even row's (low) nibble of the byte at P to N, which has no
 * bits outside the low nibble. */
#define LORES_SET_LO(p, n) (*(p) = (*(p) & LORES_HI) | (n))

/* Set the odd row's (high) nibble of the byte at P to N, which has no
 * bits outside the high nibble. */
#define LORES_SET_HI(p, n) (*(p) = (*(p) & LORES_LO) | (n))

/* As LORES_SET_LO, for the two bytes at P, from the two in N. */
#define LORES_SET_LO2(p, n) \
  (*(lores_word *)(p) = (*(lores_word *)(p) & LORES_HI2) | (n))

/* As LORES_SET_HI, for the two bytes at P, from the two in N. */
#define LORES_SET_HI2(p, n) \
  (*(lores_word *)(p) = (*(lores_word *)(p) & LORES_LO2) | (n))

#endif
