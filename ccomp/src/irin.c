/*
 * The back end's input: read the IR file, NAME.ir, that the front end
 * wrote (approach.md 18.7; format in ../OBJECT.md), and drive stages 2
 * to 4. Each IR op goes to lowering (lower_op()); the data and string
 * records (D, R, S) are copied to the stream file as they are, in the
 * order met; and the symbol summary at the end becomes NAME.syms, with
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

/* Is label n one that the function buffer has room for? */
static int good_label(int n)
{
  return n >= 0 && n < MAX_LABELS;
}

/* Check IR op op's place and operands, then lower it. */
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
  }
  if (op == IR_FUNC) {
    in_func = 1;
    nwritten++;
  } else if (op == IR_ENDFUNC) {
    in_func = 0;
  }
  lower_op(op, a, f);
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
