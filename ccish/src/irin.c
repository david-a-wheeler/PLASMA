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
#include "diagsrc.h"
#include "bio.h"
#include "nam.h"
#include "obj.h"
#include "objfmt.h"
#include "objsym.h"
#include "ir.h"
#include "mop.h"
#include "irin.h"
#include "irloop.h"
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
void ir_damaged(void)
{
  fatal(188 /* "compiler stream file is damaged:" */,
        stream_name(base_name, SUFFIX_IR));
}

/* Read a byte of the IR file, which must not end here. */
static int get_byte(void)
{
  int c = bgetc(&in);
  if (c == EOF)
    ir_damaged();
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
    ir_damaged();
  ids = get_word();
  sum = get_word();
  nam_open(base_name);
  if (ids != nam_ids || sum != nam_sum)
    nam_stale();        /* from another run of ccpp */
  strcpy(source, nam_file(1));
  src_name = source;
  src_name_hook = 0;          /* not the front end's, in ccish */
}

/* Read a name, an id, which must be in the name file. */
static int get_id(void)
{
  int id = get_word();
  if (id < 1 || id > nam_ids)
    ir_damaged();
  return id;
}

/* Read an N record: keep a function's name's id (and, for the checks,
 * whether it returns a value). */
static void read_name(void)
{
  int f = get_word();
  if (f >= MAX_FUNCS || fn_id[f])
    ir_damaged();
#ifdef CCISH_CHECKS
  {
    int c = get_byte();
    if (c & ~NAME_RETURNS)
      ir_damaged();
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

char buf_op[IR_BUF];
int buf_a[IR_BUF];
int buf_f[IR_BUF];
char buf_pre[IR_BUF];
int nbuf;

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
  0x21, 0x21, 0x21, 0x21, 0x21, 0x21,       /* ... SHR, comparisons */
  0x21, 0x21, 0x21, 0x21,                   /* both signs */
  0x11, 0x11, 0x11,                         /* NEG, COMP, NOT */
  0x00, 0x00, 0x10, 0x10, -1,               /* LABEL ... SEL */
  0x00, -1, -1, -1,                          /* JOIN, the case table */
  0x22,                                     /* SWAP */
  0x21, 0x21, 0x21,                         /* UDIV, UMOD, USHR */
  0x01,                                     /* HEAPADDR */
  0x01, 0x00,                               /* FUNCADDR, ICALL */
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
    } else if (op == IR_ICALL) {
      pops = buf_a[i] + 1;
      pushes = buf_f[i];
    }
    if (depth < pops)
      return 0;
    depth += pushes - pops;
    if (IR_IS_BRANCH(op)) {
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
    ir_damaged();
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

/* The relational comparison that means the same with its operands
 * exchanged; valid only on an IR_IS_RELATIONAL op. (Not IR_COMPARE_FLIP:
 * that is the opposite comparison, the one ! gives. */
int ir_mirror(int cmp)
{
  return cmp == IR_LT ? IR_GT : cmp == IR_GT ? IR_LT
         : cmp == IR_LE ? IR_GE : IR_LE;
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
  if (IR_IS_RELATIONAL(next)) {
    /* The operands are exchanged, so is the comparison. */
    lower_op(ir_mirror(next), 0, -1);
    return 2;
  }
  return 1;
}

/* Is IR op op one of the unsigned comparisons? */
static int is_unsigned_cmp(int op)
{
  return IR_IS_U_COMPARE(op);
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

/* Label reference counts, by the target a branch names: lab_cnt[]
 * counts the branches that have been lowered (gone from the hold
 * buffer), buf_cnt[] the branches still held, and lab_placed[] the
 * labels whose LABEL op has been lowered. A JOIN, which re-points the
 * branches so far to its a at its f (a label joined at most once,
 * parse.c's join_label), is noted in target_of[] when held, and
 * followed to the end where a count is wanted. The truth runs'
 * lab_clean (below) reads all of this. */
static int lab_cnt[MAX_LABELS];
static int buf_cnt[MAX_LABELS];
static int target_of[MAX_LABELS]; /* a JOIN's a now goes to its f */
static char lab_placed[MAX_LABELS];

/* Count a label's references as its ops are lowered: branches
 * accumulate in lab_cnt[], and the op leaves the hold buffer, so
 * buf_cnt[] loses it; a LABEL places its label and resets the
 * counts. A JOIN's re-pointing is noted when it is held. */
static void lab_count(int op, int a)
{
  if (IR_REFS_LABEL(op)) {
    lab_cnt[a]++;
    buf_cnt[a]--;
  } else if (op == IR_LABEL) {
    lab_cnt[a] = 0;
    buf_cnt[a] = 0;
    lab_placed[a] = 1;
  }
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
    if (op != IR_SWAP)
      lab_count(op, buf_a[i]);
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

/*
 * The truth runs (approach.md, decision 31). An if whose arms both
 * return constants, in either order,
 *   cond BRFALSE L0  CONST k1  RETURN  JUMP E  LABEL L0  CONST k2
 *   RETURN
 * or the same without the JUMP when the if's other arm falls out,
 * or a ternary whose arms end in a branch to the ternary's end
 * instead of returns,
 *   cond BRFALSE L0  CONST k1  JUMP L2  LABEL L0  CONST k2  [JUMP
 *   L2]  LABEL L2
 * is the condition's truth: the arms pick out 1 and 0, or both the
 * same k. The run rewrites to the value it computes, dropping the
 * branch, labels and arms (lower_held's nbuf and the lab_* below
 * stay in step):
 *   k1 == k2:  DROP CONST k1
 *   1, then 0: the condition, already 0 or 1 when it ends in a
 *              comparison or C's ! (the lowering masks that to 0
 *              or 1), else boolified: CONST 0 NE
 *   0, then 1: its negation: NOT, or CONST 0 EQ
 * In the if spelling the run's RETURN is kept, so the implicit
 * return the parser appends after it is dead code for the peephole
 * stage; in the ternary spelling the value stays on the stack for
 * whatever follows the expression. A JUMP's label, or the ternary's
 * end label, is eaten with the run when nothing else references it,
 * so it is no barrier for that dead code either. The runs are fixed
 * suffixes, matched against the hold buffer's tail as each op is
 * held; the if spelling defers a step, so the end label after its
 * core may still be eaten. The front end's invariant that all the
 * branches to a label come before the label (parse.c's
 * place_forward) makes the reference counts complete (lab_count,
 * above lower_held).
 */

/* The pending core of an if-spelling run: buf[rp_s] holds its seven
 * ops, and the next op held may be the JUMP's label, eaten with the
 * run. */
static int rp_on, rp_s, rp_k1, rp_k2, rp_ret, rp_e;

/* The current function (fn_cur) and its parameters' words
 * (fn_params), from IR_FUNC: the tail rewrite (tail_rewrite) stores
 * the call's args into the slots ENTER leaves them, 2k for the kth
 * parameter. Nothing sits under a tail call: it is held only where
 * the body's code is, and no loop layout keeps values there (the
 * counted form's limit and count do sit under its body, but body_ok()
 * in irloop.c keeps calls out of such a body, demoting the loop). */
static int fn_cur, fn_params;

/* Are k1 then k2 the arms the run rewrites: equal arms, or the two
 * truth values in either order? */
static int run_ok(int k1, int k2)
{
  return k1 == k2 || (k1 == 0 && k2 == 1) || (k1 == 1 && k2 == 0);
}

/* Is the value a run tests, whose last op is held just before the
 * run at s, already 0 or 1: a comparison, signed or unsigned, or
 * C's !? */
static int bool_at(int s)
{
  int op = s > 0 ? buf_op[s - 1] : -1;
  return IR_IS_COMPARE(op) || op == IR_NOT;
}

/* Where a branch to label x goes, through the held JOINs. */
static int lab_final(int x)
{
  while (target_of[x] != x)
    x = target_of[x];
  return x;
}

/* Is label x referenced, through the JOINs, exactly by the own
 * branches of a run, none of them lowered? A label shared with a
 * short-circuit condition's branches (a && b branches to the same
 * false label twice) fails this. */
static int lab_clean(int x, int own)
{
  int i, n = 0;
  if (lab_placed[x])
    return 0;
  for (i = 0; i < MAX_LABELS; i++)
    if (lab_final(i) == x)
      n += lab_cnt[i] + buf_cnt[i];
  return n == own;
}

/* Rewrite the len-op run at buf[s] to the value it computes: sp 1
 * when its arms end in RETURN (kept, with its a, the words
 * returned), 0 when in a branch to the end. The run's own branches
 * leave the buffer, so buf_cnt[] loses them before the rewrite.
 * Lowers what is decidable, the whole buffer. */
static void run_do(int s, int len, int sp, int k1, int k2, int ret)
{
  int o[3][3], m = 0, i;
  if (!run_ok(k1, k2))      /* the callers gate this; be sure */
    { lower_held(nbuf); return; }
  for (i = 0; i < len; i++)
    if (IR_REFS_LABEL(buf_op[s + i]))
      buf_cnt[buf_a[s + i]]--;
  if (k1 == k2) {
    o[m][0] = IR_DROP;  o[m][1] = 0;  o[m][2] = -1;  m++;
    o[m][0] = IR_CONST; o[m][1] = k1; o[m][2] = -1;  m++;
  } else if (k1 == 0) {            /* k2 is 1: the value is !cond */
    if (bool_at(s)) {
      o[m][0] = IR_NOT; o[m][1] = 0; o[m][2] = -1;  m++;
    } else {
      o[m][0] = IR_CONST; o[m][1] = 0; o[m][2] = -1;  m++;
      o[m][0] = IR_EQ;    o[m][1] = 0; o[m][2] = -1;  m++;
    }
  } else {                         /* k1 is 1, k2 is 0: the value is
                                    * cond */
    if (!bool_at(s)) {
      o[m][0] = IR_CONST; o[m][1] = 0; o[m][2] = -1;  m++;
      o[m][0] = IR_NE;    o[m][1] = 0; o[m][2] = -1;  m++;
    }
  }
  if (sp) {
    o[m][0] = IR_RETURN; o[m][1] = ret; o[m][2] = -1;  m++;
  }
  for (i = 0; i < m; i++) {
    buf_op[s + i] = (char)o[i][0];
    buf_a[s + i] = o[i][1];
    buf_f[s + i] = o[i][2];
    buf_pre[s + i] = 0;
  }
  nbuf = s + m;
  lower_held(nbuf);
}

/* Match the hold buffer's tail against a truth run and act on it: the
 * if spelling's core sets the pending step for its end label, the
 * other shapes rewrite at once. */
static void run_match(void)
{
  int n = nbuf, s, l0, k1, k2;
  if (n < 6)
    return;
  if (buf_op[n - 1] == IR_RETURN && buf_op[n - 2] == IR_CONST
      && buf_op[n - 3] == IR_LABEL) {
    l0 = buf_a[n - 3];
    k2 = buf_a[n - 2];
    if (n > 6 && buf_op[n - 7] == IR_BRFALSE && buf_a[n - 7] == l0
        && buf_op[n - 6] == IR_CONST && buf_op[n - 5] == IR_RETURN
        && buf_op[n - 4] == IR_JUMP) {
      /* cond BRFALSE L0 C1 RETURN JUMP E LABEL L0 C2 RETURN */
      k1 = buf_a[n - 6];
      if (run_ok(k1, k2) && lab_clean(l0, 1)) {
        rp_on = 1;
        rp_s = n - 7;
        rp_k1 = k1;
        rp_k2 = k2;
        rp_ret = buf_a[n - 5];
        rp_e = buf_a[n - 4];
      }
    } else if (buf_op[n - 4] == IR_RETURN && buf_op[n - 5] == IR_CONST
               && buf_op[n - 6] == IR_BRFALSE && buf_a[n - 6] == l0
               && run_ok(buf_a[n - 5], k2) && lab_clean(l0, 1)) {
      /* cond BRFALSE L0 C1 RETURN LABEL L0 C2 RETURN */
      run_do(n - 6, 6, 1, buf_a[n - 5], k2, buf_a[n - 4]);
    }
  } else if (n > 7 && buf_op[n - 1] == IR_LABEL && buf_op[n - 2] == IR_JUMP
             && buf_a[n - 2] == buf_a[n - 1] && buf_op[n - 3] == IR_CONST
             && buf_op[n - 4] == IR_LABEL && buf_op[n - 5] == IR_JUMP
             && buf_a[n - 5] == buf_a[n - 1] && buf_op[n - 6] == IR_CONST
             && buf_op[n - 7] == IR_BRFALSE
             && buf_a[n - 7] == buf_a[n - 4]
             && run_ok(buf_a[n - 6], buf_a[n - 3])
             && lab_clean(buf_a[n - 4], 1)
             && lab_clean(buf_a[n - 1], 2)) {
    /* cond BRFALSE L0 C1 JUMP L2 LABEL L0 C2 JUMP L2 LABEL L2 */
    s = n - 8;
    run_do(s, 8, 0, buf_a[n - 6], buf_a[n - 3], 0);
  } else if (n > 5 && buf_op[n - 1] == IR_LABEL && buf_op[n - 2] == IR_CONST
             && buf_op[n - 3] == IR_LABEL && buf_op[n - 4] == IR_JUMP
             && buf_a[n - 4] == buf_a[n - 1] && buf_op[n - 5] == IR_CONST
             && buf_op[n - 6] == IR_BRFALSE
             && buf_a[n - 6] == buf_a[n - 3]
             && run_ok(buf_a[n - 5], buf_a[n - 2])
             && lab_clean(buf_a[n - 3], 1)
             && lab_clean(buf_a[n - 1], 1)) {
    /* cond BRFALSE L0 C1 JUMP L2 LABEL L0 C2 LABEL L2 */
    s = n - 6;
    run_do(s, 7, 0, buf_a[n - 5], buf_a[n - 2], 0);
  }
}

/* The held tail is a comparison and its !: the De Morgan opposite of
 * the comparison, IR_COMPARE_FLIP, is the same value, one op shorter,
 * and it holds wherever the value goes, a branch, a run or an
 * expression, so the ! is dropped. The unsigned comparisons' sign-bit
 * flips (a, and PRE_FLIP on the operand block) are kept, as they are
 * independent of the De Morgan flip. */
static void not_cmp(void)
{
  buf_op[nbuf - 2] = (char)IR_COMPARE_FLIP(buf_op[nbuf - 2]);
  nbuf--;
}

/* The held tail is a betweenness: the same local loaded twice, each time
 * compared against a constant, x >= c1 (or >) and x <= c2 (or <), the two
 * fails both branching to the same label, nothing between that could change
 * the local (only the compare and its branch). A char local (0 to 255 when
 * loaded) matches with the range a power-of-two block of bytes, within 0
 * to 255; an int local matches with the range a power-of-two block of
 * words (low <= high signed, so the range is not empty, and the width a
 * power of two from 1 to 65536), or when each load is masked to its low
 * byte (the (char) cast, or an x & 255: load, CONST 255, AND), which is a
 * byte again. Rewrite it to one load, the misalignment r subtracted, the
 * block's mask ANDed, and one compare == (the aligned base), where low and
 * high are the inclusive ends the two comparisons describe (> c1 means low
 * is c1 + 1, < c2 means high is c2 - 1), w == high - low + 1 the block's
 * width, r == (low & 0xFFFF) & (w - 1), and the mask mod - w (256 - w for
 * a byte, 65536 - w for a word) keeps the high bits: the block's low
 * log2(w) bits are free, so a value is in it exactly when those high bits,
 * after shifting off r, are the base's. Emit the general unaligned shape
 * and let the peephole stage make it best: an r of 0 drops the x - 0, an r
 * of 1 is a DECR, an r of 2 to 255 a SUBI, an r above 255 (only for a word)
 * a CW and a SUB, a constant AND an ANDI when the mask is a byte, and the
 * compare's BRFALSE meets the ISEQ as a BRNE. The (char) cast's tail is
 * four ops longer than the plain one; the rewrite shortens it, and the
 * held buffer is lowered as run_do does after a length-changing rewrite,
 * so a later IR_SWAP whose counted block covered the tail falls back to
 * the value exchange, which needs no lengths. Signed only: an unsigned
 * compare is held under its own op, so it never reaches here as a signed
 * one. */
static void between_rewrite(void)
{
  int n = nbuf;
  int load, p, k, cmp1, cmp2, lab, low, high, w, r, mod, i;
  if (n < 8)
    return;
  /* Two shapes, from p: the plain one, load const cmp BRFALSE load const
   * cmp BRFALSE, eight ops, for a char local (IR_LOCALB) or an int local
   * (IR_LOCAL); and the (char) cast on an int, load CONST 255 AND const
   * cmp BRFALSE, repeated, twelve ops. In both the same local is tested
   * twice, each fail to the same label; k is the cast's four extra ops,
   * so the plain's first fail is at n - 5, the cast's at n - 7. */
  if (buf_op[n - 8] == IR_LOCALB || buf_op[n - 8] == IR_LOCAL) {
    load = buf_op[n - 8];
    p = n - 8;
    k = 0;
    if (buf_op[n - 4] != load || buf_a[n - 8] != buf_a[n - 4]
        || buf_op[n - 7] != IR_CONST || buf_op[n - 3] != IR_CONST
        || buf_op[n - 5] != IR_BRFALSE || buf_op[n - 1] != IR_BRFALSE)
      return;               /* the same local, loaded twice */
    cmp1 = buf_op[n - 6];
    cmp2 = buf_op[n - 2];
    low = buf_a[n - 7] + (cmp1 == IR_GT);
    high = buf_a[n - 3] - (cmp2 == IR_LT);
    lab = buf_a[n - 5];
  } else if (n >= 12 && buf_op[n - 12] == IR_LOCAL && buf_op[n - 6] == IR_LOCAL
             && buf_a[n - 12] == buf_a[n - 6]
             && buf_op[n - 11] == IR_CONST && buf_a[n - 11] == 0xFF
             && buf_op[n - 10] == IR_AND
             && buf_op[n - 5] == IR_CONST && buf_a[n - 5] == 0xFF
             && buf_op[n - 4] == IR_AND
             && buf_op[n - 9] == IR_CONST && buf_op[n - 3] == IR_CONST
             && buf_op[n - 7] == IR_BRFALSE && buf_op[n - 1] == IR_BRFALSE) {
    load = IR_LOCAL;
    p = n - 12;
    k = 4;
    cmp1 = buf_op[n - 8];
    cmp2 = buf_op[n - 2];
    low = buf_a[n - 9] + (cmp1 == IR_GT);
    high = buf_a[n - 3] - (cmp2 == IR_LT);
    lab = buf_a[n - 7];
  } else
    return;
  if (cmp1 != IR_GE && cmp1 != IR_GT)
    return;
  if (cmp2 != IR_LE && cmp2 != IR_LT)
    return;
  if (buf_a[n - 1] != lab)
    return;                 /* both fails must go to the same label */
  for (i = p; i < n; i++)           /* no pending flip or stash in the tail */
    if (buf_pre[i])
      return;
  if (low > high)
    return;  /* signed empty, e.g. x > 32767 && x <= 32767, which the mod
              * arithmetic would read as the whole word, always true */
  if (load == IR_LOCALB || k) {
    if (low < 0 || high > 255)
      return;               /* the byte block must be whole bytes */
    mod = 256;
  } else
    mod = 0;                 /* 65536: (0 - w) & 0xFFFF is that mask */
  w = high - low + 1;
  if ((w & (w - 1)) != 0)
    return;                 /* the block's width must be a power of two */
  r = (low & 0xFFFF) & (w - 1);      /* the misalignment, 0 when aligned */
  /* The tail from p: the load (already there), CONST r, SUB, CONST the
   * mask, AND, CONST the aligned base, EQ, BRFALSE lab. For the plain
   * shape that is the eight ops in place; for the cast, four of its
   * twelve, the rest dropped. All the ops take no f, and the scan above
   * left no pre bits, so only the op and the a need setting. */
  buf_op[p + 1] = IR_CONST;  buf_a[p + 1] = r;
  buf_op[p + 2] = IR_SUB;    buf_a[p + 2] = 0;
  buf_op[p + 3] = IR_CONST;  buf_a[p + 3] = (mod - w) & 0xFFFF;
  buf_op[p + 4] = IR_AND;    buf_a[p + 4] = 0;
  buf_op[p + 5] = IR_CONST;  buf_a[p + 5] = (low - r) & 0xFFFF;
  buf_op[p + 6] = IR_EQ;     buf_a[p + 6] = 0;
  buf_op[p + 7] = IR_BRFALSE; buf_a[p + 7] = lab;
  buf_cnt[lab]--;           /* the dropped IR_BRFALSE no longer names lab */
  if (k) {
    nbuf = n - 4;
    lower_held(nbuf);       /* no parser length may name the tail now */
  }
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
    ir_damaged();
  c = buf_a[nbuf - 1] & 0xFFFF;
  if (*op == IR_USHR) {
    if (c > 15)
      ir_damaged();
    return c;
  }
  if (c == 0 || (c & (c - 1)) != 0)
    ir_damaged();
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
void ir_hold(int op, int a, int f)
{
  if (rp_on) {
    int eat = op == IR_LABEL && a == rp_e && lab_clean(rp_e, 1);
    rp_on = 0;
    run_do(rp_s, 7, 1, rp_k1, rp_k2, rp_ret);
    if (eat)
      return;
  }
  if (nbuf > 0 && buf_op[nbuf - 1] == IR_SWAP
      && buf_a[nbuf - 1] == SW_PENDING)
    decide(nbuf - 1, op);
  if (IR_IS_U_ARITH(op) && a == 1) {
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
  if (IR_REFS_LABEL(op))
    buf_cnt[a]++;
  else if (op == IR_JOIN)
    target_of[a] = f;
  run_match();
  if (op == IR_NOT && nbuf >= 2 && IR_IS_COMPARE(buf_op[nbuf - 2]))
    not_cmp();
  between_rewrite();
  if (op == IR_ENDFUNC)
    lower_held(nbuf);
}

/* The hold buffer's tail is the self tail call at index at, its args
 * held before it and nothing under them: lower what is held, drop
 * the call, and hold the replacement: the args' stores into the
 * parameter slots, a word each at the offsets ENTER leaves them (2k
 * for the kth), and the jump to the function's entry label, after
 * its ENTER. The jump reuses the current frame (no new ENTER per
 * recursion), and the base case's return is the function's, so the
 * call's return is dropped. */
static void tail_rewrite(int at)
{
  int i, p = fn_params;
  lower_held(at);
  nbuf = 0;                     /* the call, now at 0, is gone */
  for (i = p - 1; i >= 0; i--) {
    ir_hold(IR_SETLOCAL, 2 * i, -1);
    ir_hold(IR_DROP, 0, -1);
  }
  ir_hold(IR_JUMP, ENTRY_LABEL, -1);
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
    ir_damaged();               /* FUNC outside a function, all else in */
  switch (op) {
  case IR_FUNC: case IR_CALL: case IR_FUNCADDR:
    if (f < 0 || f >= MAX_FUNCS || !fn_id[f])
      ir_damaged();
    if (f > max_func)
      max_func = f;
    break;
  case IR_LABEL: case IR_JUMP: case IR_BRFALSE: case IR_BRTRUE:
  case IR_SEL: case IR_CASETO:
    if (!good_label(a))
      ir_damaged();
    break;
  case IR_JOIN: case IR_LOOP: case IR_LOOPSTEP: case IR_LOOPBODY:
  case IR_LOOPEND:
    if (!good_label(a) || !good_label(f))
      ir_damaged();
    break;
  case IR_SWAP:
    if (a < 0 || f < -1 || (a == 0 && f >= 0))
      ir_damaged();
    break;
  case IR_ULT: case IR_UGT: case IR_ULE: case IR_UGE:
    if (a < 0)
      ir_damaged();
    break;
  case IR_UDIV: case IR_UMOD: case IR_USHR:
    if (a < 0 || a > 1)
      ir_damaged();
    break;
  case IR_ICALL:
    if (a < 0 || f < 0 || f > MAX_BYVAL / 2)
      ir_damaged();
    break;
  case IR_RETURN:
    if (a < 0 || a > MAX_BYVAL / 2)
      ir_damaged();
    break;
  }
  if (op == IR_FUNC) {
    in_func = 1;
    fn_cur = f;
    fn_params = a;
    nwritten++;
    for (i = 0; i < MAX_LABELS; i++) {
      lab_cnt[i] = 0;
      buf_cnt[i] = 0;
      target_of[i] = i;
      lab_placed[i] = 0;
    }
  } else if (op == IR_ENDFUNC) {
    in_func = 0;
  }
  if (opt_ir)
    dump_ir(op, a, f);
  if (!loop_take(op, a, f)) {
    ir_hold(op, a, f);
    if (op == IR_RETURN && nbuf >= 2
        && buf_op[nbuf - 2] == IR_CALL && buf_f[nbuf - 2] == fn_cur)
      tail_rewrite(nbuf - 2);   /* a self tail call */
  }
  if (op == IR_FUNC)
    ir_hold(IR_LABEL, ENTRY_LABEL, -1);
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
    ir_damaged();
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
    ir_damaged();
  emit_word(nf);
  emit_word(nd);
  n = get_byte();               /* modules to import from */
  emit_byte(n);
  while (n-- > 0)
    put_name();
  for (i = 0; i < nf; i++) {
    flags = get_byte();
    emit_byte(flags);
    emit_byte(get_byte());      /* parameter count */
    get_word();                 /* code offset: 0 */
    if (!(flags & SYM_DEFINED) != (fn_code[i] < 0))
      ir_damaged();
    emit_word(flags & SYM_DEFINED ? fn_code[i] : 0);
    ndefined += (flags & SYM_DEFINED) != 0;
    emit_word(get_word());      /* type hash */
    put_name();
    if (flags & SYM_FIMPORT) {
      emit_byte(get_byte());    /* its module */
      put_name();               /* its PLASMA name */
    }
  }
  if (ndefined != nwritten)
    ir_damaged();               /* a function written twice */
  for (i = 0; i < nd; i++) {
    emit_byte(flags = get_byte());
    copy(6);                    /* offset, size, type hash */
    put_name();
    if (flags & SYM_DIMPORT) {
      emit_byte(get_byte());
      put_name();
    }
  }
  if (bgetc(&in) != EOF)
    ir_damaged();
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
  loop_reset();
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
    if (t < IR_TAG_END || (t >= IR_BYTE_OP && t < IR_BYTE_OP + IR_TAG_END
                           && prefixed < 2)) {
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
      ir_damaged();             /* a prefix not followed by its op */
    if (REC_IS_PASSTHROUGH(t)) {
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
      ir_damaged();
    }
  }
  bclose(&in);
  nam_close();
  src_line = 0;
}

/* After an error in the back end: close its open files (ProDOS needs it). */
void gen_abort(void)
{
  bclose(&in);
  nam_close();
  emit_abort();
}
