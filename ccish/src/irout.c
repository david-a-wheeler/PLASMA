// The front end's output: the writer of the IR file, NAME.ir, which
// the back end (ccgen) reads (approach.md 18.7; format in ../OBJECT.md).
// The file holds, in the order they are made:
//   - the IR ops, as the parser puts them;
//   - the data and string records (D, R, S), exactly as the stream file
//     will have them, written as the parser declares data;
//   - a line record before each function, and a name record before the
//     first op that refers to a function, for ccgen's messages and -i;
//   - the symbol summary, last, with its code offsets 0, which only
//     ccgen knows.
//
// The parser reorders code without a buffer of its own: it counts the
// ops written (ir_here()), and puts IR_SWAP with the lengths of the two
// blocks to exchange, which ccgen does (approach.md 18.7, A).
#include <stdio.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.src.h"
#include "bio.h"
#include "nam.h"
#include "obj.h"
#include "obj.fmt.h"
#include "obj.sym.h"
#include "ir.h"
#include "type.h"
#include "parse.h"
#include "ptop.h"
#include "irout.h"

#define DATA_RUN    128     // bytes in one data record at most

// The IR file, and its output name (for messages).
static Bfile out;
static const char *base_name;

static int ndata;               // bytes in the data so far
static int nstrs;               // bytes in the string pool so far

// Data bytes not yet written: the current run, written as one D record
// when it is full, or before an R record or the end of the stream; and
// after it, zpend zeros, held back until something else comes, so that
// an initializer of only zeros can be taken back (data_drop_zeros())
// and its object put in a zero-filled space instead.
static unsigned char drun[DATA_RUN];
static int ndrun;
static int zpend;

static int nops;                // IR ops written so far

// Create the IR file for output name base, and write its header: the
// magic and the version. (The name file's stamp is in the summary, at
// the end, since it's at the end of the token stream too.)
void ir_open(const char *base)
{
  base_name = base;
  if (!bopen(&out, stream_name(base, SUFFIX_IR), 1))
    fatal_d(176 /* "cannot create" */, stream_name(base, SUFFIX_IR));
  bwrite(&out, (const unsigned char *)"CIR", 3);
  bputc(&out, IR_VERSION);
}

// Write a name record for function f, if it has none yet: its name's
// id, and whether it returns a value (for ccgen's checks).
static void name_func(int f)
{
  if (fn_flags[f] & FN_NAMED)
    return;
  fn_flags[f] |= FN_NAMED;
  bputc(&out, IR_REC(REC_NAME));
  bputw(&out, f);
  bputc(&out, fn_type[f] != TY_VOID ? NAME_RETURNS : 0);
  bputw(&out, fn_name[f]);
}

// Stage 1's output: write one IR op, its records first (a function's
// line, a name), then its operands' prefixes, then its tag. a is 0, a
// byte after the tag, or a W prefix's word; f (if not -1) is an X
// prefix's word.
void ir_put(int op, int a, int f)
{
  nops++;
  if (op == IR_FUNC) {
    bputc(&out, IR_REC(REC_LINE));
    bputw(&out, src_line);
  }
  if (IR_NAMES_FUNC(op))
    name_func(f);
  if (f != -1) {                    // -1 is the reader's default
    bputc(&out, IR_REC(REC_SECOND));
    bputw(&out, f);
  }
  if (a == 0) {
    bputc(&out, op);
  } else if (a > 0 && a < 256) {
    bputc(&out, op | IR_BYTE_OP);
    bputc(&out, a);
  } else {
    bputc(&out, IR_REC(REC_WORD));
    bputw(&out, a);
    bputc(&out, op);
  }
}

// The number of IR ops written so far: the IR position of the next.
// The parser takes the lengths of blocks to exchange from it.
int ir_here(void)
{
  return nops;
}

// ---------- Data and strings ----------

// Write a string literal of len bytes to the stream, for the string
// pool. Returns the pool offset of its first character.
int data_string(const char *s, int len)
{
  int first = nstrs + 1;
  // Keep offsets well inside 16 bits; the final pass checks that the
  // whole module fits.
  if (nstrs > 0x7FFF - len - 2)
    error0(181 /* "too many string literals: over 32767 bytes" */);
  // In the pool it is [len][chars][NUL] (the final pass adds the length
  // byte and the NUL): the C pointer is to the first character, and the
  // byte before it makes it a PLASMA string as well.
  bputc(&out, IR_REC(REC_STR));
  bputw(&out, len);
  bwrite(&out, (const unsigned char *)s, len);
  nstrs += len + 2;
  return first;
}

// Write the pending data run as a D record.
static void flush_data(void)
{
  if (ndrun == 0)
    return;
  bputc(&out, IR_REC(REC_DATA));
  bputc(&out, ndrun);
  bwrite(&out, drun, ndrun);
  ndrun = 0;
}

// Append byte c to the pending data run, writing the run first if full.
static void run_byte(int c)
{
  if (ndrun == DATA_RUN)
    flush_data();
  drun[ndrun++] = (unsigned char)c;
}

// Write the zeros held back: a Z record of them, after the pending
// run, unless they're so few (under 4) that the bytes are shorter.
static void put_zeros(void)
{
  if (zpend >= 4) {
    flush_data();
    bputc(&out, IR_REC(REC_ZERO));
    bputw(&out, zpend);
    zpend = 0;
  }
  for (; zpend > 0; zpend--)
    run_byte(0);
}

// The number of bytes written to the data stream so far: the data
// offset of the next byte.
int data_offset(void)
{
  return ndata;
}

// Append one byte (the low byte of v) to the data: a zero is held back.
void data_byte(int v)
{
  if (ndata >= 0x7FFF)
    error0(182 /* "too much data: globals over 32767 bytes" */);
  ndata++;
  if ((v & 0xFF) == 0) {
    zpend++;
    return;
  }
  put_zeros();
  run_byte(v);
}

// If the data from offset mark on is all zeros held back, take them
// back (the data ends at mark again) and return 1; else return 0.
int data_drop_zeros(int mark)
{
  if (ndata - zpend > mark)
    return 0;
  zpend -= ndata - mark;
  ndata = mark;
  return 1;
}

// Append a word to the data that holds an address, plus add bytes: of
// data (kind REL_DATA) or of a string (REL_STR), at offset target, of
// data symbol target (REL_DSYM, an extern global), or of function
// target (REL_FUNC; add is 0). It is an R record
// in place of the word's two bytes; the final pass writes the address
// there.
void data_addr(int kind, int target, int add)
{
  if (ndata > 0x7FFF - 2)
    error0(182 /* "too much data: globals over 32767 bytes" */);
  put_zeros();
  flush_data();
  bputc(&out, IR_REC(REC_DREL));
  bputc(&out, kind);
  if (kind == REL_DSYM) {
    bputw(&out, target);
    bputw(&out, add);
  } else {
    bputw(&out, target + add);
  }
  ndata += 2;
}

// Reserve n zero bytes in the data, held back (put_zeros() writes them
// as a Z record, or bytes if fewer than 4). Returns their data offset.
int data_zero(int n)
{
  int first = ndata;
  if (ndata > 0x7FFF - n)
    error0(182 /* "too much data: globals over 32767 bytes" */);
  ndata += n;
  zpend += n;                   // held back, as data_byte()'s
  return first;
}

// ---------- The end ----------

// Write the symbol summary as the last record, and close the file: the
// name file's stamp (../CCPP.md), which ccgen checks against the name
// file before it spells a name into NAME.syms; then the functions,
// then the global variables (data symbols), in NAME.syms's
// layout after its magic and version, but with the code size and
// offsets 0 (ccgen fills them in) and each name as its id (ccgen
// spells it).
void ir_finish(void)
{
  int i, flags, off, size, hash, name, dep, label;
  int nsyms = nglobals;           // the data symbols

  put_zeros();
  flush_data();
  bputc(&out, IR_REC(REC_SUMMARY));
  bputw(&out, nam_ids);           // the stamp of the name file the ids
  bputw(&out, nam_sum);           // are from, from the token stream
  bputw(&out, ndata);
  bputw(&out, nstrs);
  bputw(&out, 0);
  bputw(&out, zero_next[ZERO_GLOBALS]);
  bputw(&out, zero_next[ZERO_AREA]);
  bputw(&out, nfuncs);
  bputw(&out, nsyms);
  bputc(&out, dep_count());       // the modules pragmas named
  for (i = 0; i < dep_count(); i++)
    bputw(&out, dep_id[i]);
  for (i = 0; i < nfuncs; i++) {
    flags = func_sym(i, &hash);
    bputc(&out, flags);
    bputc(&out, fn_flags[i] & FN_COUNTED ? fn_nparams[i] : 255);
    bputw(&out, 0);
    bputw(&out, hash);
    bputw(&out, fn_name[i]);
    dep = sym_import(i, fn_name[i], &label);
    if (dep >= 0) {               // SYM_FIMPORT: its module and name
      bputc(&out, dep);
      bputw(&out, label);
    }
  }
  for (i = 0; i < nsyms; i++) {
    flags = data_sym(i, &off, &size, &hash, &name);
    bputc(&out, flags);
    bputw(&out, off);
    bputw(&out, size);
    bputw(&out, hash);
    bputw(&out, name);
    dep = sym_import(IMP_DATA + i, name, &label);
    if (dep >= 0) {               // SYM_DIMPORT
      bputc(&out, dep);
      bputw(&out, label);
    }
  }
  if (bclose(&out) != 0)
    fatal_d(187 /* "error writing" */, stream_name(base_name, SUFFIX_IR));
}

// After an error: close the IR file if it is open, writing out what is
// buffered, so that a kept file (-k) is complete up to the error, and
// so that ProDOS can remove it.
void ir_abort(void)
{
  if (out.f) {
    put_zeros();
    flush_data();
  }
  bclose(&out);
}
