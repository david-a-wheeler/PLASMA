/*
 * ccpp's #if and #elif expressions (approach.md, "ccpp design"; C11
 * 6.10.1): the rest of the directive's line, with macros expanded and
 * defined(name) or defined name replaced by 1 or 0, then every other
 * name by 0. Integer constants and C's operators but the assignments,
 * ++, -- and the comma; parentheses; ?:. Arithmetic is ccish's 16-bit
 * int and unsigned, with C's usual conversions (C says intmax_t and
 * uintmax_t: a documented difference). && || and ?: don't evaluate the
 * operand they skip, so 0 && 1 / 0 is no error.
 *
 * A value is kept as its 16 bits (0 to $FFFF) in an int, and read as
 * signed (sx()) where that matters, so that the same code is right in a
 * 32-bit int (the host) and a 16-bit one (ccish).
 */
#include <stdio.h>
#include "build.h"
#include "diag.h"
#include "bio.h"
#include "token.h"
#include "fixed.h"
#include "ppname.h"
#include "pplex.h"
#include "ppmac.h"
#include "ppif.h"

static int cur;                 /* the current token's code */
static int cur_val;             /* and its value */
static int uns;                 /* the last value is unsigned */
static int live;                /* evaluating, not skipping */

/* The expression is malformed: stop. */
static void bad(void)
{
  error(259 /* "bad expression in #if" */, NULL);
}

/* The 16 bits of v, read as a signed value. */
static int sx(int v)
{
  v &= 0xFFFF;
  return v >= 0x8000 ? v - 0x8000 - 0x8000 : v;
}

/* Move to the next token: a number for defined(name) or defined name,
 * and 0 for any other name (or keyword) left after expansion. */
static void advance(void)
{
  int paren;
  cur = pp_next();
  cur_val = pt_val;
  if (cur == TC_ID && pt_val == ID_DEFINED) {
    cur = pp_next_raw();        /* the name isn't expanded */
    paren = cur == '(';
    if (paren)
      cur = pp_next_raw();
    if (cur != TC_ID && cur != TC_KEYWORD)
      bad();
    cur_val = pp_is_macro(pt_val);
    if (paren && pp_next_raw() != ')')
      bad();
    cur = TC_NUM;
  } else if (cur == TC_ID || cur == TC_KEYWORD
             || (cur >= TC_KEYWORDS && cur < TC_KEYWORDS + NKEYWORDS)) {
    cur = TC_NUM;
    cur_val = 0;
  }
}

/* The precedence of binary operator op (higher binds tighter), or -1 if
 * it isn't one. */
static int prec(int op)
{
  switch (op) {
  case '*': case '/': case '%': return 10;
  case '+': case '-': return 9;
  case TC_SHL: case TC_SHR: return 8;
  case '<': case '>': case TC_LE: case TC_GE: return 7;
  case TC_EQ: case TC_NE: return 6;
  case '&': return 5;
  case '^': return 4;
  case '|': return 3;
  case TC_ANDAND: return 2;
  case TC_OROR: return 1;
  }
  return -1;
}

static int cond(void);

/* A unary expression: an operator and its operand, a parenthesized
 * expression, or a number. */
static int unary(void)
{
  int op = cur, v;
  if (op == '+' || op == '-' || op == '~' || op == '!') {
    advance();
    v = unary();
    if (op == '-')
      v = -v;
    else if (op == '~')
      v = ~v;
    else if (op == '!') {
      v = (v & 0xFFFF) == 0;
      uns = 0;
    }
    return v & 0xFFFF;
  }
  if (op == '(') {
    advance();
    v = cond();
    if (cur != ')')
      bad();
    advance();
    return v;
  }
  if (op != TC_NUM && op != TC_UNUM)
    bad();
  v = cur_val & 0xFFFF;
  uns = op == TC_UNUM;
  advance();
  return v;
}

/* a op b, of 16-bit values, unsigned if u (but a shift's type is its
 * left operand's, au). */
static int apply(int op, int a, int b, int u, int au)
{
  if ((op == '/' || op == '%') && b == 0) {
    if (live)
      error(34 /* "division by zero" */, "(in #if)");
    return 0;
  }
  switch (op) {
  case '*': return (int)(((unsigned)a * (unsigned)b) & 0xFFFFu);
  case '/': return u ? (int)((unsigned)a / (unsigned)b)
                     : (sx(a) / sx(b)) & 0xFFFF;
  case '%': return u ? (int)((unsigned)a % (unsigned)b)
                     : (sx(a) % sx(b)) & 0xFFFF;
  case '+': return (a + b) & 0xFFFF;
  case '-': return (a - b) & 0xFFFF;
  case TC_SHL: return b >= 16 ? 0 : (a << b) & 0xFFFF;
  case TC_SHR:
    if (au)
      return b >= 16 ? 0 : (int)((unsigned)a >> b);
    return (sx(a) >> (b >= 16 ? 15 : b)) & 0xFFFF;
  case '<': return u ? (unsigned)a < (unsigned)b : sx(a) < sx(b);
  case '>': return u ? (unsigned)a > (unsigned)b : sx(a) > sx(b);
  case TC_LE: return u ? (unsigned)a <= (unsigned)b : sx(a) <= sx(b);
  case TC_GE: return u ? (unsigned)a >= (unsigned)b : sx(a) >= sx(b);
  case TC_EQ: return a == b;
  case TC_NE: return a != b;
  case '&': return a & b;
  case '^': return a ^ b;
  }
  return a | b;
}

/* Binary operators of precedence min and above, by precedence
 * climbing. */
static int binary(int min)
{
  int a = unary(), au = uns, b, bu, op, p, saved;
  for (;;) {
    op = cur;
    p = prec(op);
    if (p < min)
      break;
    advance();
    if (op == TC_ANDAND || op == TC_OROR) {
      saved = live;
      if ((op == TC_ANDAND) == !a)
        live = 0;               /* the result is known */
      b = binary(p + 1);
      live = saved;
      a = op == TC_ANDAND ? a && b : a || b;
      au = 0;
      continue;
    }
    b = binary(p + 1);
    bu = uns;
    a = apply(op, a, b & 0xFFFF, au || bu, au);
    if (p == 7 || p == 6)
      au = 0;                   /* a comparison is an int */
    else if (p != 8)
      au = au || bu;
  }
  uns = au;
  return a;
}

/* A conditional expression, a ? b : c, or a binary one. */
static int cond(void)
{
  int c = binary(1), t, tu, f, saved = live;
  if (cur != '?')
    return c;
  advance();
  live = saved && c;
  t = cond();
  tu = uns;
  if (cur != ':')
    bad();
  advance();
  live = saved && !c;
  f = cond();
  live = saved;
  uns = tu || uns;
  return c ? t : f;
}

/* Read and evaluate the rest of an #if or #elif line: is it true? */
int pp_if_expr(void)
{
  int v;
  live = 1;
  advance();
  if (cur == TC_NL)
    bad();                      /* no expression */
  v = cond();
  if (cur != TC_NL)
    bad();
  return (v & 0xFFFF) != 0;
}
