/*
 * The back end's input: read the IR file, NAME.ir, that the front end
 * wrote (approach.md 18.7; format in ../OBJECT.md), and drive stages 2
 * to 4. Each IR op goes to lowering (lower_op()), after waiting in a
 * buffer where the parser's reordering is done (IR_SWAP; below); the
 * data and string records (D, R, S) are copied to the stream file as
 * they are, in the order met; and the symbol summary at the end becomes
 * NAME.syms, with
 * the code size and each defined function's code offset filled in,
 * which only the back end knows, and each name spelled from the name
 * file, NAME.nam (../CCPP.md), whose stamp the IR file's header gives.
 * The file is read once, in order, and every record is checked, so that
 * a damaged file is reported rather than compiled.
 */
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "bio.h"
#include "nam.h"
#include "obj.h"
#include "ir.h"
#include "mop.h"
#include "irin.h"
#include "lower.h"
#include "emit.h"
#include "helper.h"

/* The IR file, and its output name (for messages). */
static Bfile in;
static const char *base_name;

/* The source file's name, file 1 of the name file, for messages. */
static char source[MAX_PATH];

/* The functions' names, from the N records: each one's id, or 0 if it
 * has none yet. */
static int fn_id[MAX_FUNCS];
#ifdef CCISH_CHECKS
static char fn_returns[MAX_FUNCS];  /* from the name record: 1 if the
                                     * function returns a value */
#endif

static int in_func;         /* between a FUNC and its ENDFUNC */
static int max_func;        /* the highest function index met, or -1 */
static int nwritten;        /* functions written (FUNC ops) */

/* Stop: the IR file is damaged (or cut short). */
static void damaged(void)
{
  fatal(188 /* "compiler stream file is damaged:" */,
        stream_name(base_name, SUFFIX_IR));
}

/* Read a byte of the IR file, which must not end here. */
static int get_byte(void)
{
  int c = bgetc(&in);
  if (c == EOF)
    damaged();
  return c;
}

/* Read a word, low byte first. */
static int get_word(void)
{
  int lo = get_byte();
  return lo | (get_byte() << 8);
}

/* Copy the next n bytes of the IR file to the output file. */
static void copy(int n)
{
  while (n-- > 0)
    emit_byte(get_byte());
}

/* Copy a D, R or S record, whose tag t has just been read, to the
 * stream file as it is. */
static void copy_record(int t)
{
  int n;
  emit_byte(t);
  if (t == REC_DATA) {
    n = get_byte();
    emit_byte(n);
    copy(n);
  } else if (t == REC_DREL) {
    n = get_byte();
    emit_byte(n);
    copy(n == REL_DSYM ? 4 : 2);
  } else if (t == REC_ZERO) {
    emit_word(get_word());
  } else {
    n = get_word();
    emit_word(n);
    copy(n);
  }
}

/* Read the header: the magic, the version, and the stamp of the name
 * file, which must match it; then the source file's name, which
 * messages give from now on. */
static void read_header(void)
{
  int ids, sum;
  if (get_byte() != 'C' || get_byte() != 'I' || get_byte() != 'R'
      || get_byte() != IR_VERSION)
    damaged();
  ids = get_word();
  sum = get_word();
  nam_open(base_name);
  if (ids != nam_ids || sum != nam_sum)
    nam_stale();        /* from another run of ccpp */
  strcpy(source, nam_file(1));
  src_name = source;
}

/* Read a name, an id, which must be in the name file. */
static int get_id(void)
{
  int id = get_word();
  if (id < 1 || id > nam_ids)
    damaged();
  return id;
}

/* Read an N record: keep a function's name's id (and, for the checks,
 * whether it returns a value). */
static void read_name(void)
{
  int f = get_word();
  if (f >= MAX_FUNCS || fn_id[f])
    damaged();
#ifdef CCISH_CHECKS
  {
    int c = get_byte();
    if (c & ~NAME_RETURNS)
      damaged();
    fn_returns[f] = (c & NAME_RETURNS) != 0;
  }
#else
  get_byte();
#endif
  fn_id[f] = get_id();
}

/* Function f's name, for messages and -i (or a helper's), from the name
 * file. */
const char *func_name(int f)
{
  if (f >= MAX_FUNCS)
    return helper_name(f - MAX_FUNCS);
  if (f < 0 || !fn_id[f])
    return "?";
  return nam_name(fn_id[f]);
}

/* Write a symbol's name, read as an id, to NAME.syms: its length, then
 * its characters. */
static void put_name(void)
{
  const char *s = nam_name(get_id());
  emit_byte((int)strlen(s));
  while (*s)
    emit_byte(*s++);
}

/*
 * The IR ops read but not yet lowered, as parallel arrays (approach.md
 * 18.7, A). The parser can't reorder code itself: when a value's code
 * must come before code it has already written (*f() = e: the value
 * below the address, since there is no SWAP opcode), it puts IR_SWAP
 * with the lengths of the two blocks, which are the ops just before
 * it. If both are still here, they are exchanged in place, by three
 * reversals (Bentley, Programming Pearls, column 2), and the SWAP costs
 * nothing: moving ops is safe because each lowers on its own and
 * branches name labels by number. The SWAP stays, done, so that a block
 * holding it has the length the parser counted, and lowers to nothing.
 * When the buffer is full, its older half is lowered, so the last
 * IR_BUF / 2 ops are always here: blocks that long always move.
 *
 * A SWAP that can't move code (it is told not to, or a block has been
 * lowered) waits for the op after it (SW_PENDING; buf_f holds its top
 * block's length, 0 if not known, times 2, plus 1 if that block ends in
 * a DUP: a store whose value is used, which leaves the value twice). Then it is done
 * the cheapest way left (decide()), its mode in buf_a:
 *   SW_FREE      the next op reversed, at no cost: ADD and the other
 *                commutative ops as they are, SUB as SUB then NEG, < as
 *                >, and so on (lower_free_swap())
 *   SW_STASH     the value below saved in temporary 0 just before the
 *                top block's code runs (buf_pre[] marks its first op),
 *                and loaded again after it: one temporary
 *   SW_EXCHANGE  the values exchanged after the fact through two
 *                temporaries (buf_f: the values the top block left)
 * A stash needs the top block still here, and holding no other SWAP
 * that uses a temporary, whose use would overlap.
 *
 * The unsigned comparisons (IR_ULT ...; 18.7, C) are signed ones with
 * both sign bits flipped (XOR $8000), the left operand's before the
 * right operand's code, which is the a ops just before the comparison.
 * If those are here, the flip goes in front of them (PRE_FLIP), and the
 * right one after (U_INLINE), or into the right operand itself if it is
 * one constant (U_FOLDED). If not, both are flipped afterward, the
 * right one saved in temporary 1 meanwhile (U_LATE): a stash uses
 * temporary 0, so the two never overlap.
 *
 * If the VM gains more comparison ops (approach.md, "Loop design",
 * and issue 21), this is where they would go. Unsigned comparison
 * opcodes (ISULT and the rest, not CMDSYS calls) would replace the
 * flips above. For loops, step-and-branch-if-not-equal ops (INCBRNE,
 * ADDBRNE, DECBRNE, SUBBRNE) would let every != loop, of int,
 * unsigned or a pointer, keep its count on the eval stack as PLASMA's
 * FOR does (2 ops per pass), since != has no sign; and unsigned
 * counted-loop ops (INCBRULE and the like) would do the same for
 * unsigned and pointer loops that end with < or <=. Today's counted
 * ops (INCBRLE and the rest) compare signed, so only int loops can
 * use them.
 */
enum { SW_DONE, SW_PENDING, SW_FREE, SW_STASH, SW_EXCHANGE };
enum { U_INLINE, U_FOLDED, U_LATE };
enum { PRE_STASH = 1, PRE_FLIP = 2 };   /* buf_pre[] bits */

#define IR_BUF 128
static char buf_op[IR_BUF];
static int buf_a[IR_BUF];
static int buf_f[IR_BUF];
static char buf_pre[IR_BUF];
static int nbuf;

/* Reverse the held ops from index i up to (not including) j. */
static void reverse(int i, int j)
{
  int op, a, f, pre;
  while (i < --j) {
    op = buf_op[i];
    a = buf_a[i];
    f = buf_f[i];
    pre = buf_pre[i];
    buf_op[i] = buf_op[j];
    buf_a[i] = buf_a[j];
    buf_f[i] = buf_f[j];
    buf_pre[i] = buf_pre[j];
    buf_op[j] = (char)op;
    buf_a[j] = a;
    buf_f[j] = f;
    buf_pre[j] = (char)pre;
    i++;
  }
}

#ifdef CCISH_CHECKS
/*
 * The full build's check of a block of held ops before it is moved,
 * stashed or flipped around (CCISH_CHECKS): the parser's lengths count
 * the ops it wrote, and if the two counts ever disagreed (a compiler
 * bug, or a damaged file), the wrong code would move with no message.
 * So each block must leave exactly the values IR_SWAP or the unsigned
 * comparison says (1, or 2 for a top block ending in DUP), and never
 * use a value from below it. Each op's effect is in stk_effect[]: the
 * values it pops times 16, plus those it pushes, or -1 for an op that
 * can't be in an expression. A CALL pops its arguments and pushes a
 * value if its function returns one (from the name record). Branches
 * are followed: lab_depth[] is the depth at each label the block
 * branches to (plus 1; 0 if none yet), and code after a JUMP is
 * unreachable until a label. A SWAP inside the block, done or not,
 * changes no depth.
 */
static const char stk_effect[IR_NOPS] = {
  -1, -1, 0x01, 0x01, 0x01,                 /* FUNC ... STRADDR */
  0x01, 0x11, 0x01, 0x11, 0x01,             /* locals */
  0x01, 0x11, 0x01, 0x11,                   /* globals */
  0x11, 0x11, 0x20, 0x20,                   /* LOAD ... STOREB */
  0x00, 0x10, 0x12, -1,                     /* CALL, DROP, DUP, RETURN */
  0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,
  0x21, 0x21, 0x21, 0x21, 0x21, 0x21,       /* comparisons */
  0x11, 0x11, 0x11,                         /* NEG, COMP, NOT */
  0x00, 0x00, 0x10, 0x10, 0x00,             /* LABEL ... JOIN */
  -1, -1, -1, -1,                           /* SEL, the case table */
  0x22,                                     /* SWAP */
  0x21, 0x21, 0x21, 0x21,                   /* ULT ... */
  0x21, 0x21, 0x21,                         /* UDIV, UMOD, USHR */
  0x01,                                     /* HEAPADDR */
  -1, -1, -1, -1                            /* loop markers: never held */
};

static char lab_depth[MAX_LABELS];

/* Note that a branch reaches label n at depth: it must agree with any
 * other branch there. Returns 0 if it doesn't. */
static int branch_depth(int n, int depth)
{
  if (lab_depth[n] && lab_depth[n] != depth + 1)
    return 0;
  lab_depth[n] = (char)(depth + 1);
  return 1;
}

/* Does the block of the len held ops from index start leave want
 * values, never using one from below it? */
static int block_ok(int start, int len, int want)
{
  int i, op, e, pops, pushes, d, depth = 0, live = 1;
  for (i = 0; i < MAX_LABELS; i++)
    lab_depth[i] = 0;
  for (i = start; i < start + len; i++) {
    op = buf_op[i];
    if ((e = stk_effect[op]) < 0)
      return 0;
    if (op == IR_LABEL) {
      if ((d = lab_depth[buf_a[i]]) != 0) {
        if (live && depth != d - 1)
          return 0;
        depth = d - 1;
      } else if (!live) {
        return 0;               /* a label nothing in the block reaches */
      }
      live = 1;
      continue;
    }
    if (!live)
      continue;
    pops = e >> 4;
    pushes = e & 15;
    if (op == IR_CALL) {
      pops = buf_a[i];
      pushes = fn_returns[buf_f[i]];
    }
    if (depth < pops)
      return 0;
    depth += pushes - pops;
    if (op == IR_JUMP || op == IR_BRFALSE || op == IR_BRTRUE) {
      if (!branch_depth(buf_a[i], depth))
        return 0;
      live = op != IR_JUMP;
    } else if (op == IR_JOIN && (d = lab_depth[buf_a[i]]) != 0) {
      if (!branch_depth(buf_f[i], d - 1))
        return 0;
      lab_depth[buf_a[i]] = 0;
    }
  }
  return live && depth == want;
}

/* Stop unless block_ok(start, len, want). */
static void check_block(int start, int len, int want)
{
  if (!block_ok(start, len, want))
    damaged();
}
#endif

/* Is IR op op one whose operands can be reversed at no cost? */
static int reversible(int op)
{
  switch (op) {
  case IR_ADD: case IR_MUL: case IR_AND: case IR_OR: case IR_XOR:
  case IR_EQ: case IR_NE: case IR_SUB:
  case IR_LT: case IR_GT: case IR_LE: case IR_GE:
    return 1;
  }
  return 0;
}

/* Choose how to do the pending SWAP at held index s, now that the op
 * after it, next, is known. */
static void decide(int s, int next)
{
  int len = buf_f[s] >> 1, values = 1 + (buf_f[s] & 1), i;
  if (values == 1 && reversible(next)) {
    buf_a[s] = SW_FREE;
    return;
  }
  if (len >= 1 && len <= s) {
    for (i = s - len; i < s; i++)
      if (buf_op[i] == IR_SWAP && buf_a[i] >= SW_STASH)
        break;
    if (i == s) {
#ifdef CCISH_CHECKS
      check_block(s - len, len, values);
#endif
      buf_pre[s - len] |= PRE_STASH;
      buf_a[s] = SW_STASH;
      return;
    }
  }
  buf_a[s] = SW_EXCHANGE;
  buf_f[s] = values;
}

/* Lower the SW_FREE SWAP at held index i with the op after it, and
 * return how many ops that took: just the SWAP (which needs no code)
 * before a commutative op, or both. */
static int lower_free_swap(int i)
{
  int next = buf_op[i + 1];
  if (next == IR_SUB) {
    lower_op(IR_SUB, 0, -1);    /* x - y is -(y - x) */
    lower_op(IR_NEG, 0, -1);
    return 2;
  }
  if (next >= IR_LT && next <= IR_GE) {
    /* LT, GT and LE, GE are adjacent pairs in the IR's numbering
     * (ir.h): each is the other with bit 0 flipped. */
    lower_op(next ^ 1, 0, -1);
    return 2;
  }
  return 1;
}

/* Is IR op op one of the unsigned comparisons? */
static int is_unsigned_cmp(int op)
{
  return op >= IR_ULT && op <= IR_UGE;
}

/* Lower an XOR $8000: flip the top value's sign bit. */
static void lower_flip(void)
{
  lower_op(IR_CONST, -32768, -1);
  lower_op(IR_XOR, 0, -1);
}

/* Lower unsigned comparison op, in way how (U_*). */
static void lower_unsigned(int op, int how)
{
  if (how == U_INLINE) {
    lower_flip();
  } else if (how == U_LATE) {
    lower_flip();
    lower_op(IR_SETLOCAL, FRAME_TEMP(1), -1);
    lower_op(IR_DROP, 0, -1);
    lower_flip();
    lower_op(IR_LOCAL, FRAME_TEMP(1), -1);
  }
  lower_op(op, 0, -1);
}

/* Lower the oldest n held ops (or one more, if a SWAP takes the op
 * after it), keeping the rest. A SWAP is never lowered before it has
 * been decided, with the op after it held. */
static void lower_held(int n)
{
  int i = 0, j, op;
  while (i < n) {
    op = buf_op[i];
    if (op == IR_SWAP && (buf_a[i] == SW_PENDING
                          || (buf_a[i] == SW_FREE && i + 1 >= nbuf)))
      break;
    if (buf_pre[i] & PRE_STASH) {
      lower_op(IR_SETLOCAL, FRAME_TEMP(0), -1);
      lower_op(IR_DROP, 0, -1);
    }
    if (buf_pre[i] & PRE_FLIP)
      lower_flip();
    if (is_unsigned_cmp(op)) {
      lower_unsigned(op, buf_a[i]);
    } else if (op != IR_SWAP) {
      lower_op(op, buf_a[i], buf_f[i]);
    } else if (buf_a[i] == SW_FREE) {
      i += lower_free_swap(i) - 1;
    } else if (buf_a[i] == SW_STASH) {
      lower_op(IR_LOCAL, FRAME_TEMP(0), -1);
    } else if (buf_a[i] == SW_EXCHANGE) {
      lower_op(IR_SWAP, buf_f[i], -1);
    }
    i++;
  }
  for (j = i; j < nbuf; j++) {
    buf_op[j - i] = buf_op[j];
    buf_a[j - i] = buf_a[j];
    buf_f[j - i] = buf_f[j];
    buf_pre[j - i] = buf_pre[j];
  }
  nbuf -= i;
}

/* Rewrite unsigned op *op (IR_UDIV, IR_UMOD or IR_USHR) on a constant
 * right operand, the held CONST just before it, to be done inline, and
 * return its f: x >> c is USHR with f = c (SHR, then a mask); x / 2^n
 * the same, with the CONST made n; x % 2^n an AND (f -1), with the
 * CONST made 2^n - 1. The parser marks only those (a = 1); the rest
 * call helpers (lower.c). The number of ops held stays the same, as
 * blocks' lengths need. */
static int unsigned_const(int *op)
{
  int c, n = 0;
  if (nbuf == 0 || buf_op[nbuf - 1] != IR_CONST)
    damaged();
  c = buf_a[nbuf - 1] & 0xFFFF;
  if (*op == IR_USHR) {
    if (c > 15)
      damaged();
    return c;
  }
  if (c == 0 || (c & (c - 1)) != 0)
    damaged();
  if (*op == IR_UMOD) {
    buf_a[nbuf - 1] = c - 1;
    *op = IR_AND;
    return -1;
  }
  while (c > 1) {
    c >>= 1;
    n++;
  }
  buf_a[nbuf - 1] = n;
  *op = IR_USHR;
  return n;
}

/* Hold IR op op until it is lowered. A pending SWAP just before it is
 * decided first. A SWAP whose two blocks are here is done at once, by
 * moving them; one that can't be waits. An unsigned comparison's way is
 * chosen now (in a). The end of a function lowers all. */
static void hold(int op, int a, int f)
{
  if (nbuf > 0 && buf_op[nbuf - 1] == IR_SWAP
      && buf_a[nbuf - 1] == SW_PENDING)
    decide(nbuf - 1, op);
  if ((op == IR_UDIV || op == IR_UMOD || op == IR_USHR) && a == 1) {
    f = unsigned_const(&op);
    a = 0;
  }
  if (is_unsigned_cmp(op)) {
    if (a >= 1 && a <= nbuf) {
#ifdef CCISH_CHECKS
      check_block(nbuf - a, a, 1);
#endif
      buf_pre[nbuf - a] |= PRE_FLIP;
      if (a == 1 && buf_op[nbuf - 1] == IR_CONST) {
        buf_a[nbuf - 1] ^= 0x8000;
        a = U_FOLDED;
      } else {
        a = U_INLINE;
      }
    } else {
      a = U_LATE;
    }
  }
  if (op == IR_SWAP) {
    if (f >= 0 && a + f <= nbuf) {
#ifdef CCISH_CHECKS
      check_block(nbuf - a - f, f, 1);
      check_block(nbuf - a, a,
                  1 + (a > 0 && buf_op[nbuf - 1] == IR_DUP));
#endif
      reverse(nbuf - a - f, nbuf - a);
      reverse(nbuf - a, nbuf);
      reverse(nbuf - a - f, nbuf);
      a = SW_DONE;
    } else {
      f = 2 * a + (nbuf > 0 && buf_op[nbuf - 1] == IR_DUP);
      a = SW_PENDING;
    }
  }
  if (nbuf == IR_BUF)
    lower_held(IR_BUF / 2);
  buf_op[nbuf] = (char)op;
  buf_a[nbuf] = a;
  buf_f[nbuf] = f;
  buf_pre[nbuf] = 0;
  nbuf++;
  if (op == IR_ENDFUNC)
    lower_held(nbuf);
}

/*
 * Loops (approach.md, "Loop design"). The parser writes a loop in
 * source order, between markers that name its labels:
 *   LOOP Ltest Lbrk  test  LOOPSTEP Lcont Lbrk  step
 *   LOOPBODY Lbody Ltest  body  LOOPEND Lcont Lbrk
 * where the test branches to Lbody when true. Here the test and step
 * are held (lp_op[] and the rest) instead of lowered, and lowered after
 * the body, so that each pass runs one branch:
 *   BRNCH Ltest  Lbody: body  Lcont: step  Ltest: test  Lbrk:
 * The entry BRNCH is left out when the test is just a jump to Lbody
 * (for (;;)), and a held loop's labels are placed only if something
 * branches to them (lab_ref[]), since a label stops the peephole
 * stage's patterns, and code after a jump that no label follows can
 * be dropped (while (0)). Loops nest: held headers form a stack in lp_op[], one
 * loop's test from lp_test[d], its step from lp_step[d], up to the
 * next loop's or nlp; only the innermost open header is ever filling.
 * A header that doesn't fit, or a loop nested more than LOOP_NEST deep,
 * is lowered in place, as the jumps around the step would have been
 * (no held state is needed then: the markers name every label):
 *   Ltest: test  BRNCH Lbrk  Lcont: step  BRNCH Ltest
 *   Lbody: body  BRNCH Lcont  Lbrk:
 */
#define LOOP_BUF  128       /* held ops, for every open loop's header,
                             * and a counted loop's body */
#define LOOP_NEST 8         /* loops held at once */
static char lp_op[LOOP_BUF];
static int lp_a[LOOP_BUF];
static int lp_f[LOOP_BUF];
static int nlp;
static int lp_test[LOOP_NEST];  /* where each open loop's test starts */
static int lp_step[LOOP_NEST];  /* and its step, or -1 until LOOPSTEP */
static int lp_ltest[LOOP_NEST]; /* its test's label */
static int lp_lcont[LOOP_NEST]; /* and its continue and break labels, */
static int lp_lbrk[LOOP_NEST];  /* from LOOPSTEP */
static int lp_lbody[LOOP_NEST]; /* and its body's, from LOOPBODY */
static char lp_held[LOOP_NEST]; /* its header is held */
static int lp_fvar[LOOP_NEST];  /* the DLW fix: its variable, or -1 */
static char lp_fload[LOOP_NEST];    /* and how the test loads it */
static char lp_fpost[LOOP_NEST];    /* the step is v++ or v-- */
static int lp_depth;            /* loops open */
static int lp_fill;             /* the innermost loop's header (1), or
                                 * its body (2: it may be counted), is
                                 * being held */
static char lab_ref[MAX_LABELS];    /* a branch to it has been read */

/*
 * Counted loops (approach.md, "Loop design", step 2): a loop whose test
 * and step have the shape of a counted loop (counted_shape()) has its
 * body held too (lp_fill 2, from cl_body), so that at LOOPEND ccgen has
 * seen all of it, and can keep the count and the limit on the eval
 * stack, as PLASMA's FOR does: 2 or 3 ops of the loop's own work per
 * pass, instead of 7. The body must not change the loop variable
 * (cl_var, a frame offset) or anything the limit reads (limit_ok()),
 * take the addresses of the variable or the limit's locals, call anything
 * (the two values stay on the eval stack across a call, and a body
 * that recursed would fill it), or hold a loop; nor, in a loop
 * inside another loop, store through a pointer (a pointer to the
 * variable might be taken later in the outer loop, before this one
 * runs again). Their addresses mustn't have been taken earlier in the
 * function either (addr_taken[]). Anything else in the body, or a body
 * too big for lp_op[], makes it a loop like any other (body_plain()).
 * A return in the body first drops the two values under its result,
 * which waits meanwhile in temporaries (replay_body()).
 *
 * A loop that isn't counted, whose test starts by pushing a local v
 * (LOCAL v, or LOCALB v for a char) and holds no SWAP (whose blocks
 * could reach back past Ltest), and whose step ends by storing v
 * (SETLOCAL v or SETLOCALB v, then DROP), gets the DLW fix (approach.md,
 * "The DLW fix", and "for any store"): the entry pushes v before it
 * jumps to Ltest, and the step, without its DROP, keeps the v it
 * stores (DLW or DLB), so the test's own load is left out:
 *   LLW v  BRNCH Ltest  Lbody: body  Lcont: step, ending DLW v
 *   Ltest: the rest of the test
 * one op less per pass, in the same bytes. v++ and v-- (LOCAL v, DUP,
 * CONST 1, ADD or SUB, SETLOCAL v, DROP, DROP: the old value is
 * dropped last) are lowered without the DUP and both DROPs, which is
 * ++v (lp_fpost[]).
 */
static int cl_body;             /* where the held body starts */
static int cl_var, cl_cmp, cl_step, cl_nested;
/* The limit: held ops cl_ls up to cl_le, computed once on entry; a
 * constant if cl_const (then cl_lim). The locals it reads (cl_loc[]),
 * the globals (cl_gf[] the symbol or -1, cl_ga[] the offset), and
 * whether it loads through an address (cl_load), which the body then
 * mustn't change. */
#define LIM_NAMES 4
static int cl_ls, cl_le, cl_const, cl_lim, cl_load;
static int cl_loc[LIM_NAMES], cl_nloc;
static int cl_gf[LIM_NAMES], cl_ga[LIM_NAMES], cl_ngl;
static unsigned char addr_taken[32];    /* frame offsets: a bit each */

/* Hold a jump to label n. */
static void jump(int n)
{
  lab_ref[n] = 1;
  hold(IR_JUMP, n, -1);
}

/* Hold label n, if something branches to it. */
static void label(int n)
{
  if (lab_ref[n])
    hold(IR_LABEL, n, -1);
}

/* Hold machine branch op to label n (a counted loop's). */
static void mop_branch(int op, int n)
{
  lab_ref[n] = 1;
  hold(IR_MOP, op, n);
}

/* Lower held ops from from up to (not including) to. */
static void replay(int from, int to)
{
  for (; from < to; from++)
    hold(lp_op[from], lp_a[from], lp_f[from]);
}

/* The innermost loop's header doesn't fit: lower what it holds, and
 * the rest of the loop in place. */
static void loop_overflow(void)
{
  int d = lp_depth - 1, t = lp_test[d], s = lp_step[d];
  hold(IR_LABEL, lp_ltest[d], -1);
  replay(t, s < 0 ? nlp : s);
  if (s >= 0) {
    jump(lp_lbrk[d]);
    hold(IR_LABEL, lp_lcont[d], -1);
    replay(s, nlp);
  }
  nlp = t;
  lp_held[d] = 0;
  lp_fill = 0;
}

/* Hold the jump into open loop d's test, after pushing the variable for
 * the DLW fix. */
static void enter_test(int d)
{
  if (lp_fvar[d] >= 0)
    hold(lp_fload[d], lp_fvar[d], -1);
  jump(lp_ltest[d]);
}

/* The innermost loop's held body can't be counted: lower what it holds,
 * as step 1 does (its test is never just a jump), and the rest as it
 * comes. */
static void body_plain(void)
{
  int d = lp_depth - 1;
  enter_test(d);
  label(lp_lbody[d]);
  replay(cl_body, nlp);
  nlp = cl_body;
  lp_fill = 0;
}

/* Hold IR op op (with a and f) of the header or body being filled. */
static void keep(int op, int a, int f)
{
  if (nlp == LOOP_BUF) {
    if (lp_fill == 2)
      body_plain();
    else
      loop_overflow();
    hold(op, a, f);
    return;
  }
  lp_op[nlp] = (char)op;
  lp_a[nlp] = a;
  lp_f[nlp] = f;
  nlp++;
}

/* Has frame offset off's address been taken in this function? */
static int taken(int off)
{
  return (addr_taken[off >> 3] >> (off & 7)) & 1;
}

/* Is frame offset off the loop variable's, or a local limit's (a word
 * each, so a byte of either)? */
static int counted_var(int off)
{
  int i;
  if (off - cl_var <= 1 && cl_var - off <= 1)
    return 1;
  for (i = 0; i < cl_nloc; i++)
    if (off - cl_loc[i] <= 1 && cl_loc[i] - off <= 1)
      return 1;
  return 0;
}

/* Might a store to global offset a (of symbol f, or -1) change what the
 * limit reads? Any global store might, if the limit loads through an
 * address (an array's element); else one that overlaps a global it
 * reads (a word or a byte, so within a byte either way). */
static int limit_global(int a, int f)
{
  int i;
  if (cl_load)
    return 1;
  for (i = 0; i < cl_ngl; i++)
    if (cl_gf[i] == f && a - cl_ga[i] <= 1 && cl_ga[i] - a <= 1)
      return 1;
  return 0;
}

/* Are held ops from up to (not including) to a limit a counted loop can
 * compute once: one value, of constants, locals other than the variable
 * (whose addresses haven't been taken), globals, addresses, loads
 * through them, and arithmetic, with no side effects? Sets cl_loc[]
 * and the rest. A load through an address of $C000 or above (the
 * Apple's I/O space, where a read may differ each time) isn't one. */
static int limit_ok(int from, int to)
{
  int depth = 0, hw = 0, op, a;
  cl_nloc = cl_ngl = cl_load = 0;
  for (; from < to; from++) {
    op = lp_op[from];
    a = lp_a[from];
    switch (op) {
    case IR_CONST:
      hw |= (a & 0xFFFF) >= 0xC000;
      depth++;
      break;
    case IR_LOCAL: case IR_LOCALB:
      if ((a - cl_var <= 1 && cl_var - a <= 1) || taken(a))
        return 0;               /* the variable, or reachable */
      if (!counted_var(a)) {
        if (cl_nloc == LIM_NAMES)
          return 0;
        cl_loc[cl_nloc++] = a;
      }
      depth++;
      break;
    case IR_GLOBAL: case IR_GLOBALB:
      if (cl_ngl == LIM_NAMES)
        return 0;
      cl_gf[cl_ngl] = lp_f[from];
      cl_ga[cl_ngl++] = a;
      depth++;
      break;
    case IR_DATAADDR: case IR_HEAPADDR: case IR_STRADDR:
      depth++;
      break;
    case IR_LOAD: case IR_LOADB: case IR_NEG: case IR_COMP:
      if (depth < 1)
        return 0;
      cl_load |= op == IR_LOAD || op == IR_LOADB;
      break;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_AND: case IR_OR: case IR_XOR: case IR_SHL: case IR_SHR:
      if (depth < 2)
        return 0;
      depth--;
      break;
    default:
      return 0;
    }
  }
  return depth == 1 && !(hw && cl_load);
}

/* May IR op op (with a and f) be in a counted loop's body? */
static int body_ok(int op, int a, int f)
{
  switch (op) {
  case IR_CALL: case IR_UDIV: case IR_UMOD: case IR_USHR:
    return 0;
  case IR_SETLOCAL: case IR_SETLOCALB: case IR_LOCALADDR:
    return !counted_var(a);
  case IR_STORE: case IR_STOREB:
    return !cl_nested && !cl_load && !cl_ngl;  /* a pointer might reach */
  case IR_SETGLOBAL: case IR_SETGLOBALB:
    return !limit_global(a, f);
  }
  return 1;
}

/* The comparison that means the same with its operands exchanged. */
static int mirror(int cmp)
{
  return cmp == IR_LT ? IR_GT : cmp == IR_GT ? IR_LT
         : cmp == IR_LE ? IR_GE : IR_LE;
}

/* Is open loop d's held step (up to nlp) v++ (LOCAL v, DUP, CONST k,
 * ADD or SUB, SETLOCAL v, DROP, DROP), or ++v or v += k (the same
 * without the DUP and a DROP), for a local v? If so, set *v, and *k
 * (negative for SUB). */
static int step_shape(int d, int *v, int *k)
{
  int st = lp_step[d], n = nlp - st, u;
  if (n != 5 && n != 7)
    return 0;
  u = n == 7;                   /* the DUP of v++ */
  if (lp_op[st] != IR_LOCAL || (u && lp_op[st + 1] != IR_DUP)
      || lp_op[st + 1 + u] != IR_CONST
      || (lp_op[st + 2 + u] != IR_ADD && lp_op[st + 2 + u] != IR_SUB)
      || lp_op[st + 3 + u] != IR_SETLOCAL
      || lp_a[st + 3 + u] != lp_a[st] || lp_op[nlp - 1] != IR_DROP
      || lp_op[st + 4 + u] != IR_DROP)
    return 0;
  *v = lp_a[st];
  *k = lp_op[st + 2 + u] == IR_ADD ? lp_a[st + 1 + u] : -lp_a[st + 1 + u];
  return 1;
}

/* Does open loop d's held header suit the DLW fix? If so, set
 * lp_fvar[d] and the rest; else lp_fvar[d] is -1. */
static void dlw_fix(int d)
{
  int t = lp_test[d], st = lp_step[d], v = lp_a[t], load = lp_op[t];
  int store = load == IR_LOCAL ? IR_SETLOCAL : IR_SETLOCALB, i;
  lp_fvar[d] = -1;
  if (st - t < 2 || (load != IR_LOCAL && load != IR_LOCALB))
    return;
  for (i = t; i < st; i++)
    if (lp_op[i] == IR_SWAP)
      return;
  if (nlp - st == 7 && lp_op[st] == load && lp_a[st] == v
      && lp_op[st + 1] == IR_DUP && lp_op[st + 2] == IR_CONST
      && (lp_op[st + 3] == IR_ADD || lp_op[st + 3] == IR_SUB)
      && lp_op[st + 4] == store && lp_a[st + 4] == v
      && lp_op[st + 5] == IR_DROP && lp_op[st + 6] == IR_DROP) {
    lp_fpost[d] = 1;            /* v++ or v-- */
  } else if (nlp - st >= 2 && lp_op[nlp - 1] == IR_DROP
             && lp_op[nlp - 2] == store && lp_a[nlp - 2] == v) {
    lp_fpost[d] = 0;            /* any step that ends storing v */
  } else {
    return;
  }
  lp_fvar[d] = v;
  lp_fload[d] = (char)load;
}

/* Does the held header of open loop d (body label lbody) have the shape
 * of a counted loop? If so, set cl_var and the rest. The step is one
 * that step_shape() knows; the test is
 * LOCAL v and the limit (CONST or another LOCAL), in either order,
 * compared signed (LT, LE, GT or GE: unsigned ones are ULT and the
 * rest), then BRTRUE lbody. Counting up must end at < or <=, down at
 * > or >=. */
static int counted_shape(int d, int lbody)
{
  int t = lp_test[d], st = lp_step[d];
  if (!step_shape(d, &cl_var, &cl_step))
    return 0;
  if (st - t < 4 || lp_op[st - 1] != IR_BRTRUE || lp_a[st - 1] != lbody)
    return 0;
  cl_cmp = lp_op[st - 2];
  if (cl_cmp != IR_LT && cl_cmp != IR_LE && cl_cmp != IR_GT
      && cl_cmp != IR_GE)
    return 0;
  if (lp_op[t] == IR_LOCAL && lp_a[t] == cl_var) {
    cl_ls = t + 1;              /* v, then the limit */
    cl_le = st - 2;
  } else if (lp_op[st - 3] == IR_LOCAL && lp_a[st - 3] == cl_var) {
    cl_ls = t;                  /* the limit, then v */
    cl_le = st - 3;
    cl_cmp = mirror(cl_cmp);
  } else {
    return 0;
  }
  cl_const = cl_le - cl_ls == 1 && lp_op[cl_ls] == IR_CONST;
  cl_lim = lp_a[cl_ls];
  if (!limit_ok(cl_ls, cl_le) || taken(cl_var) || cl_step == 0
      || (cl_step > 0) != (cl_cmp == IR_LT || cl_cmp == IR_LE))
    return 0;
  cl_nested = d > 0;
  return 1;
}

/* Is IR op op a push with no side effects, of one word? */
static int pure_push(int op)
{
  return op == IR_CONST || op == IR_LOCAL || op == IR_LOCALB
         || op == IR_GLOBAL || op == IR_GLOBALB || op == IR_DATAADDR
         || op == IR_STRADDR || op == IR_HEAPADDR;
}

/* Lower a counted loop's held body, from from up to (not including) to:
 * before each RETURN, its result's words (its a) are stored in
 * temporaries, the loop's limit and count dropped from under them, and
 * the words pushed again. A one-word result that is one push just
 * before the RETURN (return i;) is all of it, so the two are just
 * dropped before that push. */
static void replay_body(int from, int to)
{
  int w, k;
  for (; from < to; from++) {
    if (from + 1 < to && lp_op[from + 1] == IR_RETURN
        && lp_a[from + 1] == 1 && pure_push(lp_op[from])) {
      hold(IR_DROP, 0, -1);
      hold(IR_DROP, 0, -1);
      hold(lp_op[from], lp_a[from], lp_f[from]);
      hold(IR_RETURN, 1, -1);
      from++;
      continue;
    }
    if (lp_op[from] == IR_RETURN) {
      w = lp_a[from];
      for (k = w; k-- > 0;) {
        hold(IR_SETLOCAL, FRAME_TEMP(k), -1);
        hold(IR_DROP, 0, -1);
      }
      hold(IR_DROP, 0, -1);
      hold(IR_DROP, 0, -1);
      for (k = 0; k < w; k++)
        hold(IR_LOCAL, FRAME_TEMP(k), -1);
    }
    hold(lp_op[from], lp_a[from], lp_f[from]);
  }
}

/* At LOOPEND, write the counted loop whose body is held (labels:
 * lp_lbody[d] the top, lcont, lp_ltest[d] the test, lbrk the exit).
 * The init just before the loop, still in the swap buffer, may be
 * folded in: a CONST or a LOCAL stored into the variable, which is
 * pushed as the count instead (the loop always stores the variable
 * itself). Then:
 *   <limit> <count>  [entry]  top: DLW v  body  cont: <bottom>
 *   exit: SLW v  DROP
 * For v < n (and v > n), the bottom steps the count and tests it
 * (INCR or ADDI k, BRLT top), and the entry jumps to the test; for
 * v <= n, v < N (constant: the limit N - 1), and the same counting
 * down, the bottom is INCBRLE (or CB k ADDBRLE, DECBRGE, CB k SUBBRGE)
 * and the entry skips an empty loop (BRGT, or BRLT, exit). With both
 * ends constant, the entry goes; if such a loop would never run, it is
 * written as step 1 writes any loop. Returns 0 if not written. */
static int counted_loop(int d, int lcont, int lbrk)
{
  int up = cl_step > 0, k = up ? cl_step : -cl_step, lim = cl_lim;
  int fold = 0, c = 0, sure = 0, bottom, top = lp_lbody[d];
  int ltest = lp_ltest[d];
  if (nbuf >= 3 && buf_op[nbuf - 1] == IR_DROP
      && buf_op[nbuf - 2] == IR_SETLOCAL && buf_a[nbuf - 2] == cl_var
      && (buf_op[nbuf - 3] == IR_CONST || buf_op[nbuf - 3] == IR_LOCAL)
      && !buf_pre[nbuf - 3] && !buf_pre[nbuf - 2] && !buf_pre[nbuf - 1]
      && (nbuf == 3 || buf_op[nbuf - 4] != IR_SWAP)) {
    fold = buf_op[nbuf - 3];
    c = buf_a[nbuf - 3];
  }
  if (fold == IR_CONST && cl_const) {
    sure = cl_cmp == IR_LT ? c < lim : cl_cmp == IR_LE ? c <= lim
           : cl_cmp == IR_GT ? c > lim : c >= lim;
    if (!sure)
      return 0;                 /* it never runs */
  }
  /* The bottom: a step-and-branch op (INCBRLE ...), or a test (BRLT,
   * BRGT) after the step. */
  if (cl_cmp == IR_LE || cl_cmp == IR_GE) {
    bottom = up ? (k == 1 ? OP_INCBRLE : OP_ADDBRLE)
                : (k == 1 ? OP_DECBRGE : OP_SUBBRGE);
  } else if (k == 1 && cl_const && lim != (up ? -0x8000 : 0x7FFF)) {
    bottom = up ? OP_INCBRLE : OP_DECBRGE;
    lim += up ? -1 : 1;         /* v < N is v <= N - 1 */
  } else {
    bottom = up ? OP_BRLT : OP_BRGT;
  }
  if (fold)
    nbuf -= 3;                  /* the init's store: done by the loop */
  if (cl_const)
    hold(IR_CONST, lim, -1);
  else
    replay(cl_ls, cl_le);       /* the limit, computed once */
  hold(fold ? fold : IR_LOCAL, fold ? c : cl_var, -1);
  if (!sure) {
    if (bottom == OP_BRLT || bottom == OP_BRGT)
      jump(ltest);
    else
      mop_branch(up ? OP_BRGT : OP_BRLT, lbrk);   /* an empty loop */
  }
  lab_ref[top] = 1;
  hold(IR_LABEL, top, -1);
  hold(IR_SETLOCAL, cl_var, -1);          /* DLW v */
  replay_body(cl_body, nlp);
  label(lcont);
  if (bottom == OP_BRLT || bottom == OP_BRGT) {
    hold(IR_CONST, k, -1);
    hold(up ? IR_ADD : IR_SUB, 0, -1);    /* INCR, ADDI k, ... */
    label(ltest);
  } else if (k != 1) {
    hold(IR_CONST, k, -1);
  }
  mop_branch(bottom, top);
  label(lbrk);
  hold(IR_SETLOCAL, cl_var, -1);          /* SLW v */
  hold(IR_DROP, 0, -1);
  hold(IR_DROP, 0, -1);                   /* the limit */
  return 1;
}

/* Lay out a loop at its marker op, with labels a and f. */
static void loop_op(int op, int a, int f)
{
  int d = lp_depth - 1, held;
  if (op == IR_LOOP) {
    if (lp_fill)
      damaged();                /* a loop in a header */
    d = lp_depth++;
    if (d < LOOP_NEST) {
      lp_held[d] = 1;
      lp_test[d] = nlp;
      lp_step[d] = -1;
      lp_ltest[d] = a;
      lp_fill = 1;
    } else {
      hold(IR_LABEL, a, -1);
    }
    return;
  }
  if (d < 0)
    damaged();                  /* a marker outside a loop */
  held = d < LOOP_NEST && lp_held[d];
  if (held != (lp_fill == 1) && op != IR_LOOPEND)
    damaged();                  /* a marker where it can't be */
  if (op == IR_LOOPSTEP) {
    if (held) {
      lp_step[d] = nlp;
      lp_lcont[d] = a;
      lp_lbrk[d] = f;
    } else {
      jump(f);
      hold(IR_LABEL, a, -1);
    }
  } else if (op == IR_LOOPBODY) {
    if (held) {
      lp_fill = 0;
      if (lp_step[d] < 0)
        damaged();
      lp_lbody[d] = a;
      dlw_fix(d);
      if (counted_shape(d, a)) {
        lp_fill = 2;            /* hold the body too */
        cl_body = nlp;
        return;
      }
      if (lp_step[d] - lp_test[d] != 1 || lp_op[lp_test[d]] != IR_JUMP
          || lp_a[lp_test[d]] != a)
        enter_test(d);          /* enter at the test */
      label(a);
    } else {
      jump(f);
      hold(IR_LABEL, a, -1);
    }
  } else {
    if (lp_fill == 1)
      damaged();
    if (lp_fill == 2 && counted_loop(d, a, f)) {
      lp_fill = 0;
      nlp = lp_test[d];
      lp_depth--;
      return;
    }
    if (lp_fill == 2)
      body_plain();
    if (held && lp_fvar[d] >= 0) {
      /* The DLW fix: the step keeps v, which the test needn't push. */
      label(a);
      if (lp_fpost[d]) {
        replay(lp_step[d], lp_step[d] + 1);     /* without the DUP */
        replay(lp_step[d] + 2, nlp - 2);        /* and the DROPs */
      } else {
        replay(lp_step[d], nlp - 1);    /* all but its DROP */
      }
      label(lp_ltest[d]);
      replay(lp_test[d] + 1, lp_step[d]);
      nlp = lp_test[d];
    } else if (held) {
      label(a);
      replay(lp_step[d], nlp);
      label(lp_ltest[d]);
      replay(lp_test[d], lp_step[d]);
      nlp = lp_test[d];
    } else {
      jump(a);
    }
    label(f);
    lp_depth--;
  }
}

/* Is label n one that the function buffer has room for? */
static int good_label(int n)
{
  return n >= 0 && n < MAX_LABELS;
}

/* Check IR op op's place and operands, print it for -i, and hold it
 * for lowering. */
static void do_op(int op, int a, int f)
{
  int i;
  if (op >= IR_NOPS || (op == IR_FUNC) == in_func)
    damaged();                  /* FUNC outside a function, all else in */
  switch (op) {
  case IR_FUNC: case IR_CALL:
    if (f < 0 || f >= MAX_FUNCS || !fn_id[f])
      damaged();
    if (f > max_func)
      max_func = f;
    break;
  case IR_LABEL: case IR_JUMP: case IR_BRFALSE: case IR_BRTRUE:
  case IR_SEL: case IR_CASETO:
    if (!good_label(a))
      damaged();
    break;
  case IR_JOIN: case IR_LOOP: case IR_LOOPSTEP: case IR_LOOPBODY:
  case IR_LOOPEND:
    if (!good_label(a) || !good_label(f))
      damaged();
    break;
  case IR_SWAP:
    if (a < 0 || f < -1 || (a == 0 && f >= 0))
      damaged();
    break;
  case IR_ULT: case IR_UGT: case IR_ULE: case IR_UGE:
    if (a < 0)
      damaged();
    break;
  case IR_UDIV: case IR_UMOD: case IR_USHR:
    if (a < 0 || a > 1)
      damaged();
    break;
  case IR_RETURN:
    if (a < 0 || a > MAX_BYVAL / 2)
      damaged();
    break;
  }
  if (op == IR_FUNC) {
    in_func = 1;
    nwritten++;
    for (i = 0; i < MAX_LABELS; i++)
      lab_ref[i] = 0;
    for (i = 0; i < 32; i++)
      addr_taken[i] = 0;
  } else if (op == IR_ENDFUNC) {
    if (lp_depth)
      damaged();                /* a loop not ended */
    in_func = 0;
  }
  if (op == IR_JUMP || op == IR_BRFALSE || op == IR_BRTRUE
      || op == IR_SEL || op == IR_CASETO)
    lab_ref[a] = 1;
  else if (op == IR_JOIN)
    lab_ref[f] = 1;             /* a's branches go to f */
  if (op == IR_LOCALADDR && a >= 0 && a < 256)
    addr_taken[a >> 3] |= (unsigned char)(1 << (a & 7));
  if (opt_ir)
    dump_ir(op, a, f);
  if (lp_fill == 2 && (op == IR_LOOP || !body_ok(op, a, f)))
    body_plain();               /* this loop can't be counted */
  if (op >= IR_LOOP)
    loop_op(op, a, f);
  else if (lp_fill)
    keep(op, a, f);
  else
    hold(op, a, f);
}

/* Read the summary, the last record, and write NAME.syms from it, with
 * the code size and the defined functions' offsets filled in, then the
 * helpers this object holds (written first, after its functions): each
 * one's number, size, checksum and name. Each function the summary says
 * is defined must have been written, and no other. */
static void read_summary(void)
{
  int nf, nd, i, flags, n, ndefined = 0;
  const char *s;
  if (in_func)
    damaged();
  gen_helpers();
  emit_summary();
  emit_word(get_word());        /* data size */
  emit_word(get_word());        /* string pool size */
  get_word();                   /* code size: 0 */
  emit_word(emit_code_size());
  emit_word(get_word());        /* zero-filled (BSS) size */
  nf = get_word();
  nd = get_word();
  if (nf > MAX_FUNCS || max_func >= nf)
    damaged();
  emit_word(nf);
  emit_word(nd);
  for (i = 0; i < nf; i++) {
    flags = get_byte();
    emit_byte(flags);
    emit_byte(get_byte());      /* parameter count */
    get_word();                 /* code offset: 0 */
    if (!(flags & SYM_DEFINED) != (fn_code[i] < 0))
      damaged();
    emit_word(flags & SYM_DEFINED ? fn_code[i] : 0);
    ndefined += (flags & SYM_DEFINED) != 0;
    emit_word(get_word());      /* type hash */
    put_name();
  }
  if (ndefined != nwritten)
    damaged();                  /* a function written twice */
  for (i = 0; i < nd; i++) {
    emit_byte(get_byte());      /* flags */
    copy(6);                    /* offset, size, type hash */
    put_name();
  }
  if (bgetc(&in) != EOF)
    damaged();
  for (i = n = 0; i < NHELPERS; i++)
    n += (helper_used >> i) & 1;
  emit_byte(n);
  for (i = 0; i < NHELPERS; i++) {
    if (!(helper_used & (1 << i)))
      continue;
    emit_byte(i);
    emit_word(helper_size[i]);
    emit_word(helper_sum[i]);
    s = helper_name(i);
    emit_byte((int)strlen(s));
    while (*s)
      emit_byte(*s++);
  }
  emit_finish();
}

/* The back end: read the IR file of output name base, and write its
 * stream file and symbol summary. */
void gen_file(const char *base)
{
  int t, a = 0, f = -1, prefixed = 0, i;

  base_name = base;
  for (i = 0; i < MAX_FUNCS; i++)
    fn_id[i] = 0;
  helper_used = 0;
  nbuf = 0;
  nlp = 0;
  lp_depth = 0;
  lp_fill = 0;
  in_func = 0;
  max_func = -1;
  nwritten = 0;
  if (!bopen(&in, stream_name(base, SUFFIX_IR), 0))
    fatal(171 /* "cannot open" */, stream_name(base, SUFFIX_IR));
  read_header();
  emit_open(base);
  for (;;) {
    t = get_byte();
    /* Prefixes: X (f), then W (a word a), just before their op. */
    if (t == REC_SECOND && !prefixed) {
      f = get_word();
      prefixed = 1;
      continue;
    }
    if (t == REC_WORD && prefixed < 2) {
      a = (get_word() ^ 0x8000) - 0x8000;   /* sign-extended */
      prefixed = 2;
      continue;
    }
    /* An IR op: below IR_TAG_END, or that plus IR_BYTE_OP, with a byte
     * a after it (not after a W prefix). */
    if (t < IR_TAG_END || ((t & 0xC0) == IR_BYTE_OP && prefixed < 2)) {
      if (t & IR_BYTE_OP) {
        a = get_byte();
        t -= IR_BYTE_OP;
      }
      do_op(t, a, f);
      a = 0;
      f = -1;
      prefixed = 0;
      continue;
    }
    if (prefixed)
      damaged();                /* a prefix not followed by its op */
    if (t == REC_DATA || t == REC_DREL || t == REC_STR || t == REC_ZERO) {
      copy_record(t);
    } else if (t == REC_LINE) {
      src_line = get_word();
      if (opt_ir) {
        fputs("; line ", stdout);
        fputs(num_text(src_line), stdout);
        fputs("\n", stdout);
      }
    } else if (t == REC_NAME) {
      read_name();
    } else if (t == REC_SUMMARY) {
      read_summary();
      break;
    } else {
      damaged();
    }
  }
  bclose(&in);
  nam_close();
  src_line = 0;
}

/* After an error in the back end: close its open files (ProDOS needs
 * it). */
void gen_abort(void)
{
  bclose(&in);
  nam_close();
  emit_abort();
}
