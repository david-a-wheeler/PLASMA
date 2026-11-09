/*
 * ccgen's helper functions (approach.md, "unsigned and helper functions
 * design"): unsigned arithmetic that PLASMA has no opcode for, as small
 * functions that ccgen writes into each object that calls one (an H
 * record, after its functions), of which ccrel keeps one copy. Each is a
 * table of IR ops, lowered through ccgen's own stages like a function,
 * as function number HELPER_FN(k). They are leaves: no calls, and no
 * relocations. Their parameters are n at frame offset 0 and d (or the
 * count) at 2, as the caller pushed them; locals follow.
 *
 * PLASMA's DIV, SHR and comparisons are signed, so:
 *   - n >=u d is (n ^ $8000) >= (d ^ $8000), signed;
 *   - n >> 1 logical is (n >> 1) & $7FFF;
 *   - n / d unsigned, for d < $8000: q = ((n >> 1) / d) << 1 is the
 *     quotient or one less (Hacker's Delight, 9-3), so it is q, plus 1
 *     if r = n - q * d is still at least d. For d >= $8000, the quotient
 *     is 1 if n >= d, else 0.
 * The ops are lowered directly, not through irin.c's buffer, so the
 * unsigned comparisons are written out with their sign flips.
 */
#include "build.h"
#include "cclimits.h"
#include "obj.h"
#include "obj.fmt.h"
#include "ir.h"
#include "mop.h"
#include "lower.h"
#include "helper.h"

int helper_used;

/* The helpers' IR, as parallel arrays: an op, and its operand a (-32768
 * is $8000). Each helper runs from h_start[k] up to h_start[k + 1]; its
 * first op is its FUNC, with a its parameter words, and its last its
 * ENDFUNC, with a its frame size. */
static const char h_op[] = {
  /* udiv(n, d): q at 4, r at 6 */
  IR_FUNC,
  IR_LOCAL, IR_CONST, IR_LT, IR_BRFALSE,             /* d < 0: d >= $8000 */
  IR_LOCAL, IR_CONST, IR_XOR, IR_LOCAL, IR_CONST, IR_XOR, IR_GE,
  IR_RETURN,                                         /* n >=u d */
  IR_LABEL,
  IR_LOCAL, IR_CONST, IR_SHR, IR_CONST, IR_AND,      /* n >> 1, logical */
  IR_LOCAL, IR_DIV, IR_CONST, IR_SHL, IR_SETLOCAL, IR_DROP,   /* q */
  IR_LOCAL, IR_LOCAL, IR_LOCAL, IR_MUL, IR_SUB, IR_SETLOCAL,
  IR_DROP,                                           /* r = n - q * d */
  IR_LOCAL, IR_LOCAL, IR_CONST, IR_XOR, IR_LOCAL, IR_CONST, IR_XOR,
  IR_GE, IR_ADD, IR_RETURN,                          /* q + (r >=u d) */
  IR_ENDFUNC,
  /* umod(n, d): r at 4 */
  IR_FUNC,
  IR_LOCAL, IR_CONST, IR_LT, IR_BRFALSE,             /* d >= $8000 */
  IR_LOCAL, IR_LOCAL, IR_LOCAL, IR_CONST, IR_XOR, IR_LOCAL, IR_CONST,
  IR_XOR, IR_GE, IR_NEG, IR_AND, IR_SUB, IR_RETURN,  /* n - (d if n >=u d) */
  IR_LABEL,
  IR_LOCAL, IR_LOCAL, IR_CONST, IR_SHR, IR_CONST, IR_AND,
  IR_LOCAL, IR_DIV, IR_CONST, IR_SHL, IR_LOCAL, IR_MUL, IR_SUB,
  IR_SETLOCAL, IR_DROP,                              /* r = n - q * d */
  IR_LOCAL, IR_LOCAL, IR_LOCAL, IR_CONST, IR_XOR, IR_LOCAL, IR_CONST,
  IR_XOR, IR_GE, IR_NEG, IR_AND, IR_SUB, IR_RETURN,  /* r - (d if r >=u d) */
  IR_ENDFUNC,
  /* ushr(n, c): n >> c, logical */
  IR_FUNC,
  IR_LOCAL, IR_BRTRUE, IR_LOCAL, IR_RETURN,          /* c = 0: n */
  IR_LABEL,
  IR_LOCAL, IR_CONST, IR_SHR, IR_CONST, IR_AND,      /* once, logical */
  IR_LOCAL, IR_CONST, IR_SUB, IR_SHR, IR_RETURN,     /* then c - 1 more */
  IR_ENDFUNC
};

static const int h_a[] = {
  /* udiv */
  2,
  2, 0, 0, 0,
  0, -32768, 0, 2, -32768, 0, 0,
  0,
  0,
  0, 1, 0, 0x7FFF, 0,
  2, 0, 1, 0, 4, 0,
  0, 4, 2, 0, 0, 6,
  0,
  4, 6, -32768, 0, 2, -32768, 0,
  0, 0, 0,
  8,
  /* umod */
  2,
  2, 0, 0, 0,
  0, 2, 0, -32768, 0, 2, -32768,
  0, 0, 0, 0, 0, 0,
  0,
  0, 0, 1, 0, 0x7FFF, 0,
  2, 0, 1, 0, 2, 0, 0,
  4, 0,
  4, 2, 4, -32768, 0, 2, -32768,
  0, 0, 0, 0, 0, 0,
  6,
  /* ushr */
  2,
  2, 0, 0, 0,
  0,
  0, 1, 0, 0x7FFF, 0,
  2, 1, 0, 0, 0,
  4
};

/* Where each helper's ops start in h_op[] and h_a[], and the end. */
static const char h_start[NHELPERS + 1] = { 0, 43, 91, 108 };

/* Each helper's name, for messages and ccrel's listing. */
static const char *const h_names[NHELPERS + NHELP_CMDSYS] = {
  "udiv", "umod", "ushr", HELP_CMDSYS_NAMES
};

/* Helper k's name. */
const char *helper_name(int k)
{
  return k >= 0 && k < NHELPERS + NHELP_CMDSYS ? h_names[k] : "?";
}

/* Write each helper that a call needs, after the object's functions:
 * lower its IR as function HELPER_FN(k), which stage 4 writes as an H
 * record. */
void gen_helpers(void)
{
  int k, i, f;
  for (k = 0; k < NHELPERS; k++) {
    if (!(helper_used & (1 << k)))
      continue;
    f = HELPER_FN(k);
    for (i = h_start[k]; i < h_start[k + 1]; i++)
      lower_op(h_op[i], h_a[i], h_op[i] == IR_FUNC || h_op[i] == IR_ENDFUNC
                                ? f : -1);
  }
}
