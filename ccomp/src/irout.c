/*
 * The front end's output: the writer of the IR file, NAME.ir, which
 * the back end (ccgen) reads (approach.md 18.7; format in ../OBJECT.md).
 * The file holds, in the order they are made:
 *   - the IR ops, as the parser puts them;
 *   - the data and string records (D, R, S), exactly as the stream file
 *     will have them, written as the parser declares data;
 *   - a line record before each function, and a name record before the
 *     first op that refers to a function, for ccgen's messages and -i;
 *   - the symbol summary, last, with its code offsets 0, which only
 *     ccgen knows.
 *
 * The parser reorders code without a buffer of its own: it counts the
 * ops written (ir_here()), and puts IR_SWAP with the lengths of the two
 * blocks to exchange, which ccgen does (approach.md 18.7, A).
 */
#include <string.h>
#include "ccish.h"

/* The IR file, and its output name (for messages). */
static Bfile out;
static const char *base_name;

static int ndata;               /* bytes in the data so far */
static int nstrs;               /* bytes in the string pool so far */

/* Data bytes not yet written: the current run, written as one D record
 * when it is full, or before an R record or the end of the stream. */
static unsigned char drun[DATA_RUN];
static int ndrun;

static int nops;                /* IR ops written so far */

/* Create the IR file for output name base, and write its header: the
 * magic, the version, and the source file's name. */
void ir_open(const char *base)
{
  int n = (int)strlen(src_name);
  base_name = base;
  if (!bopen(&out, stream_name(base, SUFFIX_IR), 1))
    fatal(176 /* "cannot create" */, stream_name(base, SUFFIX_IR));
  bwrite(&out, (const unsigned char *)"CIR", 3);
  bputc(&out, IR_VERSION);
  if (n > 255)
    n = 255;
  bputc(&out, n);
  bwrite(&out, (const unsigned char *)src_name, n);
}

/* Write a name record for function f, if it has none yet, saying
 * whether it returns a value (for ccgen's checks). */
static void name_func(int f)
{
  const char *s;
  int n;
  if (fn_flags[f] & FN_NAMED)
    return;
  fn_flags[f] |= FN_NAMED;
  s = name_of(fn_name[f]);
  n = (int)strlen(s);
  bputc(&out, REC_NAME);
  bputw(&out, f);
  bputc(&out, n | (fn_type[f] != TY_VOID ? NAME_RETURNS : 0));
  bwrite(&out, (const unsigned char *)s, n);
}

/* Stage 1's output: write one IR op, its records first (a function's
 * line, a name), then its operands' prefixes, then its tag. a is 0, a
 * byte after the tag, or a W prefix's word; f (if not -1) is an X
 * prefix's word. */
void ir_put(int op, int a, int f)
{
  nops++;
  if (op == IR_FUNC) {
    bputc(&out, REC_LINE);
    bputw(&out, src_line);
  }
  if (op == IR_FUNC || op == IR_CALL)
    name_func(f);
  if (f >= 0) {
    bputc(&out, REC_SECOND);
    bputw(&out, f);
  }
  if (a == 0) {
    bputc(&out, op);
  } else if (a > 0 && a < 256) {
    bputc(&out, op | IR_BYTE_OP);
    bputc(&out, a);
  } else {
    bputc(&out, REC_WORD);
    bputw(&out, a);
    bputc(&out, op);
  }
}

/* The number of IR ops written so far: the IR position of the next.
 * The parser takes the lengths of blocks to exchange from it. */
int ir_here(void)
{
  return nops;
}

/* ---------- Data and strings ---------- */

/* Write a string literal of len bytes to the stream, for the string
 * pool. Returns the pool offset of its first character. */
int data_string(const char *s, int len)
{
  int first = nstrs + 1;
  /* Keep offsets well inside 16 bits; the final pass checks that the
   * whole module fits. */
  if (nstrs > 0x7FFF - len - 2)
    error(181 /* "too many string literals: over 32767 bytes" */, NULL);
  /* In the pool it is [len][chars][NUL] (the final pass adds the length
   * byte and the NUL): the C pointer is to the first character, and the
   * byte before it makes it a PLASMA string as well. */
  bputc(&out, REC_STR);
  bputw(&out, len);
  bwrite(&out, (const unsigned char *)s, len);
  nstrs += len + 2;
  return first;
}

/* Write the pending data run as a D record. */
static void flush_data(void)
{
  if (ndrun == 0)
    return;
  bputc(&out, REC_DATA);
  bputc(&out, ndrun);
  bwrite(&out, drun, ndrun);
  ndrun = 0;
}

/* The number of bytes written to the data stream so far: the data
 * offset of the next byte. */
int data_offset(void)
{
  return ndata;
}

/* Append one byte (the low byte of v) to the data. */
void data_byte(int v)
{
  if (ndata >= 0x7FFF)
    error(182 /* "too much data: globals over 32767 bytes" */, NULL);
  if (ndrun == DATA_RUN)
    flush_data();
  drun[ndrun++] = (unsigned char)v;
  ndata++;
}

/* Append a word, low byte first, to the data. */
void data_word(int v)
{
  data_byte(v);
  data_byte(v >> 8);
}

/* Append a word to the data that holds an address, plus add bytes: of
 * data (kind REL_DATA) or of a string (REL_STR), at offset target, or
 * of data symbol target (REL_DSYM, an extern global). It is an R record
 * in place of the word's two bytes; the final pass writes the address
 * there. */
void data_addr(int kind, int target, int add)
{
  if (ndata > 0x7FFF - 2)
    error(182 /* "too much data: globals over 32767 bytes" */, NULL);
  flush_data();
  bputc(&out, REC_DREL);
  bputc(&out, kind);
  if (kind == REL_DSYM) {
    bputw(&out, target);
    bputw(&out, add);
  } else {
    bputw(&out, target + add);
  }
  ndata += 2;
}

/* Reserve n zero bytes in the data, for a global variable. Returns
 * their data offset. */
int data_zero(int n)
{
  int first = ndata;
  if (ndata > 0x7FFF - n)
    error(182 /* "too much data: globals over 32767 bytes" */, NULL);
  while (n-- > 0)
    data_byte(0);
  return first;
}

/* ---------- The end ---------- */

/* Write a symbol's name (a name pool offset) to the summary: its
 * length, then its characters. */
static void put_name(int name)
{
  const char *s = name_of(name);
  int n = (int)strlen(s);
  bputc(&out, n);
  bwrite(&out, (const unsigned char *)s, n);
}

/* Write the symbol summary as the last record, and close the file: the
 * functions, then the global variables (data symbols), in NAME.syms's
 * layout after its magic and version, with the code size and offsets 0
 * (ccgen fills them in). */
void ir_finish(void)
{
  int i, flags, off, size, hash, name, nsyms = data_sym_count();

  flush_data();
  bputc(&out, REC_SUMMARY);
  bputw(&out, ndata);
  bputw(&out, nstrs);
  bputw(&out, 0);
  bputw(&out, nfuncs);
  bputw(&out, nsyms);
  for (i = 0; i < nfuncs; i++) {
    flags = func_sym(i, &hash);
    bputc(&out, flags);
    bputc(&out, fn_flags[i] & FN_COUNTED ? fn_nparams[i] : 255);
    bputw(&out, 0);
    bputw(&out, hash);
    put_name(fn_name[i]);
  }
  for (i = 0; i < nsyms; i++) {
    flags = data_sym(i, &off, &size, &hash, &name);
    bputc(&out, flags);
    bputw(&out, off);
    bputw(&out, size);
    bputw(&out, hash);
    put_name(name);
  }
  if (bclose(&out) != 0)
    fatal(187 /* "error writing" */, stream_name(base_name, SUFFIX_IR));
}

/* After an error: close the IR file if it is open, writing out what is
 * buffered, so that a kept file (-k) is complete up to the error, and
 * so that ProDOS can remove it. */
void ir_abort(void)
{
  if (out.f)
    flush_data();
  bclose(&out);
}
