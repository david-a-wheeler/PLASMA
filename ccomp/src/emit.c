/*
 * Stage 4: the emitter. Encodes machine ops into bytes and writes the
 * compiler's output as five stream files (approach.md 18.1; formats in
 * ../OBJECT.md):
 *
 *   .data  the global variables' bytes, in order, with their initial
 *          values (0 if they have none)
 *   .drel  relocation records for the words in .data that hold
 *          addresses (char *p = "hi";)
 *   .strs  the string pool: every string literal, in order. It is kept
 *          apart from .data so that literals inside a global's
 *          initializer don't land in the middle of the global.
 *   .code  for each function: its bytecode, then its relocation records
 *   .syms  the symbol summary, written when compilation ends
 *
 * Only one function's bytecode is in memory at a time, in a function
 * buffer, so that its frame size and its branch offsets can be patched
 * before it is written. Memory use doesn't depend on the size of
 * the program. The final pass (link.c) turns the streams into a module.
 */
#include <string.h>
#include "ccish.h"

static Bfile data_out, drel_out, strs_out, code_out;
static Bfile syms_out;      /* written at the end (emit_finish()) */
static const char *base_name;

static int ndata;               /* bytes written to the data stream */
static int nstrs;               /* bytes written to the string pool */
static int ncode;               /* bytes written to the code stream so far */

/* The function being built (an index into fn_name[] and the other
 * function arrays), or -1. */
static int fn = -1;
static unsigned char fbuf[MAX_FUNC_CODE];
static int nfbuf;

typedef struct {
  int kind;       /* REL_DATA, REL_STR or REL_FUNC */
  int at;         /* offset of the operand word in fbuf */
  int target;     /* data or string pool offset, or symbol index */
} Reloc;

static Reloc relocs[MAX_FUNC_RELOCS];
static int nrelocs;

/* Frame-size bytes of the current function, filled in at its end. */
static int frame_at[MAX_RETURNS];
static int nframe;

/*
 * Labels of the current function, resolved with fixup chains
 * (approach.md 18.2). Until label n is placed, label_word[n] is the
 * head of a chain of the branches waiting for it: the fbuf offset of
 * the latest one's operand word, whose own value is the offset of the
 * one before it, ending in NO_LINK. Once placed, label_word[n] is the
 * label's fbuf offset.
 */
#define NO_LINK 0xFFFF
static int label_word[MAX_LABELS];
static char label_placed[MAX_LABELS];

/* Create stream file base + suffix for writing, as b. */
static void open_stream(Bfile *b, const char *base, const char *suffix)
{
  if (!bopen(b, stream_name(base, suffix), 1))
    fatal(176 /* "cannot create" */, stream_name(base, suffix));
}

/* Create the stream files written during compilation, for output name
 * base. */
void emit_open(const char *base)
{
  base_name = base;
  open_stream(&data_out, base, SUFFIX_DATA);
  open_stream(&drel_out, base, SUFFIX_DREL);
  open_stream(&strs_out, base, SUFFIX_STRS);
  open_stream(&code_out, base, SUFFIX_CODE);
}

/* Append one byte to the function buffer. */
static void code_byte(int v)
{
  if (nfbuf >= MAX_FUNC_CODE)
    error(177 /* "function too large:" */, name_of(fn_name[fn]));
  fbuf[nfbuf++] = (unsigned char)v;
}

/* Append a 16-bit word, low byte first, to the function buffer. */
static void code_word(int v)
{
  code_byte(v & 0xFF);
  code_byte((v >> 8) & 0xFF);
}

/* Record that the next word in fbuf needs a final address. */
static void add_reloc(int kind, int target)
{
  if (nrelocs >= MAX_FUNC_RELOCS)
    error(178 /* "too many calls and data references in" */,
          name_of(fn_name[fn]));
  relocs[nrelocs].kind = kind;
  relocs[nrelocs].at = nfbuf;
  relocs[nrelocs].target = target;
  nrelocs++;
}

/* Append an ENTER or LEAVE frame-size byte. If it's FRAME_LATER, note
 * where it is, so the function's end can fill it in. */
static void frame_byte(int v)
{
  if (v == FRAME_LATER) {
    if (nframe >= MAX_RETURNS)
      error(179 /* "too many returns in" */, name_of(fn_name[fn]));
    frame_at[nframe++] = nfbuf;
    v = 0;
  }
  code_byte(v);
}

/* Put the 16-bit word v at fbuf offset at, low byte first. */
static void patch_word(int at, int v)
{
  fbuf[at] = (unsigned char)v;
  fbuf[at + 1] = (unsigned char)(v >> 8);
}

/* Append a branch's offset word for label n. A placed label gives the
 * offset now; otherwise the word joins the label's fixup chain. */
static void branch_word(int n)
{
  if (label_placed[n]) {
    code_word(label_word[n] - nfbuf);
  } else {
    int at = nfbuf;
    code_word(label_word[n]);
    label_word[n] = at;
  }
}

/* Place label n here: walk its fixup chain, giving each waiting branch
 * its offset (relative to its own operand word). */
static void place_label(int n)
{
  int at = label_word[n];
  if (label_placed[n])
    fatal(180 /* "internal error: label placed twice in" */,
          name_of(fn_name[fn]));
  while (at != NO_LINK) {
    int link = fbuf[at] | (fbuf[at + 1] << 8);
    patch_word(at, nfbuf - at);
    at = link;
  }
  label_word[n] = nfbuf;
  label_placed[n] = 1;
}

/* Write a string literal of len bytes to the string pool. Returns the
 * pool offset of its first character. */
int data_string(const char *s, int len)
{
  int first, i;
  /* Keep offsets well inside 16 bits; the final pass checks that the
   * whole module fits. */
  if (nstrs > 0x7FFF - len - 2)
    error(181 /* "too many string literals: over 32767 bytes" */, NULL);
  /* [len][chars][NUL]: the C pointer is to the first character, and
   * the byte before it makes it a PLASMA string as well. A literal
   * longer than 255 has a meaningless length byte (pstr() rejects it). */
  bputc(&strs_out, len > 255 ? 0 : len);
  first = nstrs + 1;
  for (i = 0; i < len; i++)
    bputc(&strs_out, s[i]);
  bputc(&strs_out, 0);
  nstrs += len + 2;
  return first;
}

/* The number of bytes written to the data stream so far: the data
 * offset of the next byte. */
int data_offset(void)
{
  return ndata;
}

/* Append one byte (the low byte of v) to the data stream. */
void data_byte(int v)
{
  if (ndata >= 0x7FFF)
    error(182 /* "too much data: globals over 32767 bytes" */, NULL);
  bputc(&data_out, v & 0xFF);
  ndata++;
}

/* Append a word, low byte first, to the data stream. */
void data_word(int v)
{
  data_byte(v);
  data_byte(v >> 8);
}

/* Append a word to the data stream that holds an address: of data
 * (kind REL_DATA) or of a string (REL_STR), at offset target. It is
 * written as 0, with a record in the data relocation stream; the final
 * pass fills it in. */
void data_addr(int kind, int target)
{
  bputc(&drel_out, kind);
  bputw(&drel_out, ndata);
  bputw(&drel_out, target);
  data_word(0);
}

/* Reserve n zero bytes in the data stream, for a global variable.
 * Returns their data offset. */
int data_zero(int n)
{
  int first = ndata, i;
  if (ndata > 0x7FFF - n)
    error(182 /* "too much data: globals over 32767 bytes" */, NULL);
  for (i = 0; i < n; i++)
    bputc(&data_out, 0);
  ndata += n;
  return first;
}

/* For a function whose frame is empty (no parameters, locals or
 * temporaries), drop the frame, as PLASMA's compiler does: remove the
 * ENTER, and make each LEAVE a RET. The frame size is only known at the
 * end, so this edits the finished buffer. Removing the ENTER at the
 * start moves all of the code back by the same amount, so relative
 * branch offsets stay valid. A LEAVE (2 bytes) in the middle becomes
 * RET plus a pad byte (another RET, which never runs), so that nothing
 * after it moves; a final LEAVE just becomes RET. */
static void drop_frame(void)
{
  int i;
  if (nframe == 0 || frame_at[0] != 1 || fbuf[0] != OP_ENTER)
    fatal(183 /* "internal error: no ENTER at the start of" */,
          name_of(fn_name[fn]));
  for (i = 1; i < nframe; i++) {      /* frame_at[0] is the ENTER's */
    fbuf[frame_at[i] - 1] = OP_RET;
    fbuf[frame_at[i]] = OP_RET;
  }
  if (nframe > 1 && frame_at[nframe - 1] == nfbuf - 1)
    nfbuf--;                        /* a final LEAVE needs no pad */
  memmove(fbuf, fbuf + 3, nfbuf - 3);
  nfbuf -= 3;
  for (i = 0; i < nrelocs; i++)
    relocs[i].at -= 3;
}

/* Write the finished function to the code stream. */
static void end_function(int frame_size)
{
  int i;
  if (frame_size > 255)
    error(184 /* "too many parameters and locals in" */,
          name_of(fn_name[fn]));
  for (i = 0; i < nframe; i++)
    fbuf[frame_at[i]] = (unsigned char)frame_size;
  if (frame_size == 0)
    drop_frame();
  for (i = 0; i < MAX_LABELS; i++)
    if (!label_placed[i] && label_word[i] != NO_LINK)
      fatal(185 /* "internal error: branch to a label never"
                   " placed in" */, name_of(fn_name[fn]));
  if (ncode > 0x7FFF - nfbuf)
    fatal(186 /* "too much code" */, NULL);
  fn_code[fn] = ncode;
  bputw(&code_out, fn);
  bputw(&code_out, nfbuf);
  bwrite(&code_out, fbuf, nfbuf);
  bputw(&code_out, nrelocs);
  for (i = 0; i < nrelocs; i++) {
    bputc(&code_out, relocs[i].kind);
    bputw(&code_out, relocs[i].at);
    bputw(&code_out, relocs[i].target);
  }
  ncode += nfbuf;
  fn = -1;
}

/* Stage 4's entry: encode one machine op into the function buffer. The
 * pseudo-ops start a function, or end it and write it out. */
void emit_put(Mop *m)
{
  int i;
  switch (m->op) {
  case OP_FUNC_BEGIN:
    fn = m->fn;
    nfbuf = nrelocs = nframe = 0;
    for (i = 0; i < MAX_LABELS; i++) {
      label_word[i] = NO_LINK;
      label_placed[i] = 0;
    }
    return;
  case OP_FUNC_END:
    end_function(m->a);
    return;
  case OP_LABEL:
    place_label(m->a);
    return;
  case OP_CASES:                  /* a case table: no opcode byte */
    code_byte(m->a);
    return;
  case OP_CASEVAL:
    code_word(m->a);
    return;
  case OP_CASETO:                 /* like a branch's offset word */
    branch_word(m->a);
    return;
  }
  code_byte(m->op);
  switch (m->op) {
  case OP_CB: case OP_CFFB: case OP_ANDI:
  case OP_LLA: case OP_LLB: case OP_LLW: case OP_DLB: case OP_DLW:
  case OP_SLB: case OP_SLW:
    code_byte(m->a);
    break;
  case OP_CW: case OP_LA: case OP_CALL: case OP_LAB: case OP_LAW:
  case OP_SAB: case OP_SAW: case OP_DAB: case OP_DAW:
    /* A relocated word is written as 0; the final pass fills it in. */
    if (m->fix == FIX_DATA)
      add_reloc(REL_DATA, m->a);
    else if (m->fix == FIX_STR)
      add_reloc(REL_STR, m->a);
    else if (m->fix == FIX_FUNC)
      add_reloc(REL_FUNC, m->fn);
    code_word(m->fix == FIX_NONE ? m->a : 0);
    break;
  case OP_BRNCH: case OP_BRFLS: case OP_BRTRU: case OP_BREQ:
  case OP_BRNE: case OP_BRAND: case OP_BROR: case OP_SEL:
    branch_word(m->a);
    break;
  case OP_ENTER:
    frame_byte(m->a);
    code_byte(m->b);
    break;
  case OP_LEAVE:
    frame_byte(m->a);
    break;
  }
}

/* Close stream b, written as base + suffix, checking for errors. */
static void close_stream(Bfile *b, const char *suffix)
{
  if (bclose(b) != 0)
    fatal(187 /* "error writing" */, stream_name(base_name, suffix));
}

/* After an error: close the streams that are still open, writing out
 * what is buffered, so that a kept stream (-k) is complete up to the
 * error, and so that ProDOS can remove the files. */
void emit_abort(void)
{
  bclose(&syms_out);
  bclose(&data_out);
  bclose(&drel_out);
  bclose(&strs_out);
  bclose(&code_out);
}

/* Close the streams written during compilation, and write the symbol
 * summary. */
void emit_finish(void)
{
  int i, n;
  const char *name;

  close_stream(&data_out, SUFFIX_DATA);
  close_stream(&drel_out, SUFFIX_DREL);
  close_stream(&strs_out, SUFFIX_STRS);
  close_stream(&code_out, SUFFIX_CODE);

  /* stream_name() uses one static buffer, so get this name last. */
  name = stream_name(base_name, SUFFIX_SYMS);
  if (!bopen(&syms_out, name, 1))
    fatal(176 /* "cannot create" */, name);
  bputc(&syms_out, 'C');
  bputc(&syms_out, 'I');
  bputc(&syms_out, 2);                         /* format version */
  bputw(&syms_out, ndata);
  bputw(&syms_out, nstrs);
  bputw(&syms_out, ncode);
  bputw(&syms_out, nfuncs);
  for (i = 0; i < nfuncs; i++) {
    int defined = fn_flags[i] & FN_DEFINED;
    bputc(&syms_out, (defined ? SYM_DEFINED : 0)
                | (fn_flags[i] & FN_CALLED ? SYM_CALLED : 0)
                | (fn_type[i] != TY_VOID ? SYM_RETURNS : 0));
    bputc(&syms_out, fn_flags[i] & FN_COUNTED ? fn_nparams[i] : 255);
    bputw(&syms_out, defined ? fn_code[i] : 0);
    name = name_of(fn_name[i]);
    n = (int)strlen(name);
    bputc(&syms_out, n);
    bwrite(&syms_out, (const unsigned char *)name, n);
  }
  if (bclose(&syms_out) != 0)
    fatal(187 /* "error writing" */, name);
}
