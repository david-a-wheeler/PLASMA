/*
 * The back end's input: read the IR file, NAME.ir, that the front end
 * wrote (approach.md 18.7; format in ../OBJECT.md), and drive stages 2
 * to 4. Each IR op goes to lowering (lower_op()), after waiting in a
 * buffer where the parser's reordering is done (IR_SWAP; below); the
 * data and string records (D, R, S) are copied to the stream file as
 * they are, in the order met; and the symbol summary at the end becomes
 * NAME.syms, with
 * the code size and each defined function's code offset filled in,
 * which only the back end knows. The file is read once, in order, and
 * every record is checked, so that a damaged file is reported rather
 * than compiled.
 */
#include <string.h>
#include "ccish.h"

/* The IR file, and its output name (for messages). */
static Bfile in;
static const char *base_name;

/* The source file's name, from the header, for messages. */
static char source[256];

/* The functions' names, from the N records: each function's offset in
 * fnames[], or -1 if it has none yet. */
static int fname_at[MAX_FUNCS];
static char fnames[MAX_FNAMES];
static int fnames_len;

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
  } else {
    n = get_word();
    emit_word(n);
    copy(n);
  }
}

/* Read the header: the magic, the version, and the source file's name,
 * which messages give from now on. */
static void read_header(void)
{
  int n, i;
  if (get_byte() != 'C' || get_byte() != 'I' || get_byte() != 'R'
      || get_byte() != IR_VERSION)
    damaged();
  n = get_byte();
  for (i = 0; i < n; i++)
    source[i] = (char)get_byte();
  source[n] = 0;
  src_name = source;
}

/* Read an N record: keep a function's name. */
static void read_name(void)
{
  int f = get_word(), n = get_byte(), i;
  if (f >= MAX_FUNCS || fname_at[f] >= 0 || n < 1 || n > MAX_NAME)
    damaged();
  if (fnames_len + n + 1 > MAX_FNAMES)
    fatal(224 /* "too many function names for ccgen" */, NULL);
  fname_at[f] = fnames_len;
  for (i = 0; i < n; i++)
    fnames[fnames_len++] = (char)get_byte();
  fnames[fnames_len++] = 0;
}

/* Function f's name, for messages and -i. */
const char *func_name(int f)
{
  if (f < 0 || f >= MAX_FUNCS || fname_at[f] < 0)
    return "?";
  return fnames + fname_at[f];
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
     * (ccish.h): each is the other with bit 0 flipped. */
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

/* Hold IR op op until it is lowered. A pending SWAP just before it is
 * decided first. A SWAP whose two blocks are here is done at once, by
 * moving them; one that can't be waits. An unsigned comparison's way is
 * chosen now (in a). The end of a function lowers all. */
static void hold(int op, int a, int f)
{
  if (nbuf > 0 && buf_op[nbuf - 1] == IR_SWAP
      && buf_a[nbuf - 1] == SW_PENDING)
    decide(nbuf - 1, op);
  if (is_unsigned_cmp(op)) {
    if (a >= 1 && a <= nbuf) {
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

/* Is label n one that the function buffer has room for? */
static int good_label(int n)
{
  return n >= 0 && n < MAX_LABELS;
}

/* Check IR op op's place and operands, print it for -i, and hold it
 * for lowering. */
static void do_op(int op, int a, int f)
{
  if (op >= IR_NOPS || (op == IR_FUNC) == in_func)
    damaged();                  /* FUNC outside a function, all else in */
  switch (op) {
  case IR_FUNC: case IR_CALL:
    if (f < 0 || f >= MAX_FUNCS || fname_at[f] < 0)
      damaged();
    if (f > max_func)
      max_func = f;
    break;
  case IR_LABEL: case IR_JUMP: case IR_BRFALSE: case IR_BRTRUE:
  case IR_SEL: case IR_CASETO:
    if (!good_label(a))
      damaged();
    break;
  case IR_JOIN:
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
  }
  if (op == IR_FUNC) {
    in_func = 1;
    nwritten++;
  } else if (op == IR_ENDFUNC) {
    in_func = 0;
  }
  if (opt_ir)
    dump_ir(op, a, f);
  hold(op, a, f);
}

/* Read the summary, the last record, and write NAME.syms from it, with
 * the code size and the defined functions' offsets filled in. Each
 * function it says is defined must have been written, and no other. */
static void read_summary(void)
{
  int nf, nd, i, flags, n, ndefined = 0;
  if (in_func)
    damaged();
  emit_summary();
  emit_word(get_word());        /* data size */
  emit_word(get_word());        /* string pool size */
  get_word();                   /* code size: 0 */
  emit_word(emit_code_size());
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
    n = get_byte();
    emit_byte(n);
    copy(n);
  }
  if (ndefined != nwritten)
    damaged();                  /* a function written twice */
  for (i = 0; i < nd; i++) {
    emit_byte(get_byte());      /* flags */
    copy(6);                    /* offset, size, type hash */
    n = get_byte();
    emit_byte(n);
    copy(n);
  }
  if (bgetc(&in) != EOF)
    damaged();
  emit_finish();
}

/* The back end: read the IR file of output name base, and write its
 * stream file and symbol summary. */
void gen_file(const char *base)
{
  int t, a = 0, f = -1, prefixed = 0, i;

  base_name = base;
  for (i = 0; i < MAX_FUNCS; i++)
    fname_at[i] = -1;
  fnames_len = 0;
  nbuf = 0;
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
    if (t == REC_DATA || t == REC_DREL || t == REC_STR) {
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
  src_line = 0;
}

/* After an error in the back end: close its open files (ProDOS needs
 * it). */
void gen_abort(void)
{
  bclose(&in);
  emit_abort();
}
