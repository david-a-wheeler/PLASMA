// lores.c: the Apple II lores screen's row addresses (lores.h).
//
// The 24 text rows are in three groups of eight: text row r starts at
// $400 + 128 * (r % 8) + 40 * (r / 8), the constants below.
#include "lores.h"

constexpr int PAGE1 = 0x400;         // text and lores page 1
constexpr int ROW_STRIDE = 128;      // from a row to the next in a group
constexpr int GROUP_SHIFT = 3;
constexpr int GROUP_ROWS = 1 << GROUP_SHIFT;          // rows in a group
constexpr int GROUPS = LORES_TEXT_ROWS / GROUP_ROWS;
constexpr int GROUP_STRIDE = LORES_WIDTH;     // from a group to the next

// The groups' rows fit side by side in a row stride (with 8 bytes
// unused), which makes the rows' addresses distinct.
_Static_assert(GROUPS * GROUP_STRIDE <= ROW_STRIDE, "groups fit");

// Text row r's offset from the start of the screen.
#define OFF(r) (((r) & (GROUP_ROWS - 1)) * ROW_STRIDE \
                + ((r) >> GROUP_SHIFT) * GROUP_STRIDE)

// Text row r's address.
#define R(r) ((char *)PAGE1 + OFF(r))

// Two entries, for text row r's two lores rows.
#define R2(r) R(r), R(r)

char *const lores_row[LORES_HEIGHT] = {
  R2(0),  R2(1),  R2(2),  R2(3),  R2(4),  R2(5),  R2(6),  R2(7),
  R2(8),  R2(9),  R2(10), R2(11), R2(12), R2(13), R2(14), R2(15),
  R2(16), R2(17), R2(18), R2(19), R2(20), R2(21), R2(22), R2(23)
};

