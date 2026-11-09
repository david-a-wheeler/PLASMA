/*
 * Stage 4: the emitter, the last stage of the back end (ccgen).
 * Encodes machine ops into bytes and writes the compiler's output
 * (approach.md 18.1 and 18.6 B6a; formats in ../OBJECT.md) as two
 * files:
 *
 *   .obj   one stream of tagged records, in the order they are made:
 *          D  a run of the global variables' bytes, with their initial
 *             values (0 if they have none)
 *          R  a word in the data that holds an address (char *p = "hi";)
 *          S  a string literal, for the string pool. The pool is kept
 *             apart from the data so that literals inside a global's
 *             initializer don't land in the middle of the global.
 *          F  a function: its bytecode, then its relocation records
 *   .syms  the symbol summary, written when the IR file ends
 *
 * The D, R and S records come from the IR file as they are, and irin.c
 * copies them here (emit_byte()); the F records are built here. Only
 * one function's bytecode is in memory at a time, in a function buffer,
 * so that its frame size and its branch offsets can be patched before
 * it is written. Memory use doesn't depend on the size of the program.
 * The final pass (link.c) turns the files into a module.
 */
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "bio.h"
#include "obj.h"
#include "obj.fmt.h"
#include "obj.sym.h"
#include "ir.h"
#include "mop.h"
#include "irin.h"
#include "emit.h"
#include "lower.h"
#include "helper.h"

#define MAX_RETURNS  32     /* frame-size bytes (ENTER, LEAVEs) in one
                             * function (23) */
#define MAX_TEMP_USES 32    /* operands on ccgen's own temporaries in one
                             * function: 4 to 6 for each IR_SWAP done
                             * when the code runs */

/* The output file: the stream, then the symbol summary
 * (emit_summary()). One Bfile serves both, one after the other. */
static Bfile out;
static const char *base_name;

static int ncode;               /* bytes of code written so far */

int fn_code[MAX_FUNCS];
int helper_size[NHELPERS];
int helper_sum[NHELPERS];

/* The function being built (an index into fn_name[] and the other
 * function arrays), or -1. */
static int fn = -1;

#ifdef CCISH_LISTING
/* For the listing (-d, full build only, since this buffer alone is 12K):
 * the current function's ops, kept so they are printed when the
 * function ends, once its frame size is known. A function has at most
 * one op per code byte, plus a label or a join per label. */
#define NLBUF (MAX_FUNC_CODE + 2 * MAX_LABELS)
static Mop lbuf[NLBUF];
static int nlbuf;
static int nlisted;            /* functions listed so far */
#endif
static unsigned char fbuf[MAX_FUNC_CODE];
static int nfbuf;

/* The current function's relocations, as parallel arrays (decision
 * 15): each one's kind (REL_DATA, REL_STR, REL_FUNC or REL_DSYM), the
 * fbuf offset of its operand word, and its target (a data or string
 * pool offset, or a symbol index). */
static char rel_kind[MAX_FUNC_RELOCS];
static int rel_at[MAX_FUNC_RELOCS];
static int rel_target[MAX_FUNC_RELOCS];
static int nrelocs;

/* Frame-size bytes of the current function, filled in at its end. */
static int frame_at[MAX_RETURNS];
static int nframe;

/* The operand bytes on the back end's own temporaries (FRAME_TEMP(k),
 * for IR_SWAP's run-time exchange) in the current function, filled in
 * at its end with offsets past the parser's frame: where each is, and
 * its k. */
static int temp_at[MAX_TEMP_USES];
static char temp_k[MAX_TEMP_USES];
static int ntemp;
static int temp_words;          /* how many temporaries are used */

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

/* Create output file base + suffix for writing. */
static void open_out(const char *base, const char *suffix)
{
  if (!bopen(&out, stream_name(base, suffix), 1))
    fatal(176 /* "cannot create" */, stream_name(base, suffix));
}

/* Create the stream file for output name base. No function is written
 * yet. */
void emit_open(const char *base)
{
  int i;
  base_name = base;
  for (i = 0; i < MAX_FUNCS; i++)
    fn_code[i] = -1;
  open_out(base, SUFFIX_OBJ);
}

/* Write one byte to the output file as it is: a record copied from the
 * IR file, or part of the summary. */
void emit_byte(int c)
{
  bputc(&out, c);
}

/* Write a word, low byte first, to the output file as it is. */
void emit_word(int v)
{
  bputw(&out, v);
}

/* The number of bytes of code written so far: the code segment's size,
 * once every function is written. */
int emit_code_size(void)
{
  return ncode;
}

/* Append one byte to the function buffer. */
static void code_byte(int v)
{
  if (nfbuf >= MAX_FUNC_CODE)
    error(177 /* "function too large:" */, func_name(fn));
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
          func_name(fn));
  rel_kind[nrelocs] = (char)kind;
  rel_at[nrelocs] = nfbuf;
  rel_target[nrelocs] = target;
  nrelocs++;
}

/* Append an ENTER or LEAVE frame-size byte. If it's FRAME_LATER, note
 * where it is, so the function's end can fill it in. */
static void frame_byte(int v)
{
  if (v == FRAME_LATER) {
    if (nframe >= MAX_RETURNS)
      error(179 /* "too many returns in" */, func_name(fn));
    frame_at[nframe++] = nfbuf;
    v = 0;
  }
  code_byte(v);
}

/* Append a frame operand byte for operand a: a local's frame offset,
 * or, for one of the back end's temporaries, a byte that the function's
 * end fills in. */
static void frame_operand(int a)
{
  if (a <= FRAME_TEMP(0)) {
    if (ntemp >= MAX_TEMP_USES)
      error(225 /* "too many run-time swaps in" */, func_name(fn));
    temp_at[ntemp] = nfbuf;
    temp_k[ntemp] = (char)(FRAME_TEMP(0) - a);
    if (temp_k[ntemp] >= temp_words)
      temp_words = temp_k[ntemp] + 1;
    ntemp++;
    a = 0;
  }
  code_byte(a);
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
 * its offset (relative to its own operand word). (The front end has its
 * own place_label, in parse.c; this one is the back end's.) */
static void patch_label(int n)
{
  int at = label_word[n];
  if (label_placed[n])
    fatal(180 /* "internal error: label placed twice in" */,
          func_name(fn));
  while (at != NO_LINK) {
    int link = fbuf[at] | (fbuf[at + 1] << 8);
    patch_word(at, nfbuf - at);
    at = link;
  }
  label_word[n] = nfbuf;
  label_placed[n] = 1;
}

/* Make the branches waiting for label a (not placed) go to label b:
 * each moves to b's fixup chain, or gets its offset now if b is
 * placed. a is then empty. */
static void join_labels(int a, int b)
{
  int at = label_word[a];
  while (at != NO_LINK) {
    int link = fbuf[at] | (fbuf[at + 1] << 8);
    if (label_placed[b]) {
      patch_word(at, label_word[b] - at);
    } else {
      patch_word(at, label_word[b]);
      label_word[b] = at;
    }
    at = link;
  }
  label_word[a] = NO_LINK;
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
          func_name(fn));
  for (i = 1; i < nframe; i++) {      /* frame_at[0] is the ENTER's */
    fbuf[frame_at[i] - 1] = OP_RET;
    fbuf[frame_at[i]] = OP_RET;
  }
  if (nframe > 1 && frame_at[nframe - 1] == nfbuf - 1)
    nfbuf--;                        /* a final LEAVE needs no pad */
  memmove(fbuf, fbuf + 3, nfbuf - 3);
  nfbuf -= 3;
  for (i = 0; i < nrelocs; i++)
    rel_at[i] -= 3;
}

/* Write the finished function, helper k, as an H record: its number,
 * then its code, which has no relocations. It isn't part of the
 * object's code segment: ccrel places one copy of each helper after
 * every object's code. Its size and a checksum (h * 31 + byte, as the
 * type hash) go in the summary, for ccrel to check that the objects'
 * copies agree. */
static void write_helper(int k)
{
  int i, h = 0;
  for (i = 0; i < nfbuf; i++)
    h = (h * 31 + fbuf[i]) & 0xFFFF;
  helper_size[k] = nfbuf;
  helper_sum[k] = h;
  bputc(&out, REC_HELPER);
  bputc(&out, k);
  bputw(&out, nfbuf);
  bwrite(&out, fbuf, nfbuf);
  fn = -1;
}

/* Write the finished function to the stream, as an F record. */
static void end_function(int frame_size)
{
  int i, base = frame_size;
  frame_size += 2 * temp_words;     /* the back end's temporaries */
  if (frame_size > 255)
    error(184 /* "too many parameters and locals in" */,
          func_name(fn));
  for (i = 0; i < ntemp; i++)
    fbuf[temp_at[i]] = (unsigned char)(base + 2 * temp_k[i]);
  for (i = 0; i < nframe; i++)
    fbuf[frame_at[i]] = (unsigned char)frame_size;
  if (frame_size == 0)
    drop_frame();
  for (i = 0; i < MAX_LABELS; i++)
    if (!label_placed[i] && label_word[i] != NO_LINK)
      fatal(185 /* "internal error: branch to a label never"
                   " placed in" */, func_name(fn));
  if (ncode > 0x7FFF - nfbuf)
    fatal(186 /* "too much code" */, NULL);
  if (fn >= MAX_FUNCS) {
    write_helper(fn - MAX_FUNCS);
    return;
  }
  fn_code[fn] = ncode;
  bputc(&out, REC_FUNC);
  bputw(&out, fn);
  bputw(&out, nfbuf);
  bwrite(&out, fbuf, nfbuf);
  bputw(&out, nrelocs);
  for (i = 0; i < nrelocs; i++) {
    bputc(&out, rel_kind[i]);
    bputw(&out, rel_at[i]);
    bputw(&out, rel_target[i]);
  }
  ncode += nfbuf;
  fn = -1;
}

#ifdef CCISH_LISTING
/*
 * The listing (-d, in the full build only): the machine ops as the back
 * end's final stream,
 * one per line, after the peephole stage (the code-generation tests
 * in tests/gen compare it against a hand-written expectation).
 * A function's ops are kept until it ends, so ENTER and LEAVE show
 * the frame size; a relocated word prints the name it will hold;
 * pseudo-ops print as comments.
 */

/* The mnemonic of each real op from MINUS1 ($20) to IDXAW ($BE),
 * indexed by (op - OP_MINUS1) / 2, with 0 for an op ccish never emits
 * (CN, $00 to $1E, is listed by list_op() itself). A table, not a
 * switch of returns: ccish can compile it (doc/bootstrap-test.md, B4),
 * and it takes less memory. */
static const char *const mnems[(OP_IDXAW - OP_MINUS1) / 2 + 1] = {
  "MINUS1", "BREQ", "BRNE", "LA", "LLA", "CB", "CW", 0,  /* $20 */
  "DROP", "DROP2", "DUP", 0, "ADDI", "SUBI", "ANDI", "ORI",  /* $30 */
  "ISEQ", "ISNE", "ISGT", "ISLT", "ISGE", "ISLE", "BRFLS", "BRTRU",  /* $40 */
  "BRNCH", "SEL", "CALL", "ICAL", "ENTER", "LEAVE", "RET", "CFFB",  /* $50 */
  "LB", "LW", "LLB", "LLW", "LAB", "LAW", "DLB", "DLW",  /* $60 */
  "SB", "SW", "SLB", "SLW", "SAB", "SAW", "DAB", "DAW",  /* $70 */
  "NOT", "ADD", "SUB", "MUL", "DIV", "MOD", "INCR", "DECR",  /* $80 */
  "NEG", "COMP", "AND", "IOR", "XOR", "SHL", "SHR", "IDXW",  /* $90 */
  "BRGT", "BRLT", "INCBRLE", "ADDBRLE", "DECBRGE", "SUBBRGE", 0, 0,  /* $A0 */
  "ADDLB", "ADDLW", "ADDAB", "ADDAW", "IDXLB", "IDXLW", "IDXAB", "IDXAW"   /* $B0 */
};

/* The name of what a relocated word operand will hold, after the
 * mnemonic, or the operand itself if it needs no relocation. A data
 * symbol prints its index: the IR carries names for functions only.
 */
static void list_word(Mop *m)
{
  if (m->fix == FIX_NONE) {
    out_num(" ", m->a);
  } else if (m->fix == FIX_FUNC) {
    fputs(" ", stdout);
    fputs(func_name(m->fn), stdout);
  } else if (m->fix == FIX_HELPER) {
    fputs(" ", stdout);
    fputs(helper_name(m->fn), stdout);
  } else if (m->fix == FIX_DSYM || m->fix == FIX_HEAP) {
    out_num(" var ", m->fn);
    if (m->a)
      out_num(" +", m->a);
  } else if (m->fix == FIX_DATA) {
    out_num(" data +", m->a);
  } else {
    out_num(" str +", m->a);
  }
}

/* Print one op of a function's listing; frame is its size. */
static void list_op(Mop *m, int frame)
{
  Mop p = *m;                   /* its plain op, and a mark if volatile */
  int v, vol = MOP_IS_VOLATILE(m->op);
  p.op = MOP_PLAIN(m->op);
  m = &p;
  switch (m->op) {
  case OP_LABEL:
    out_num("    ; label ", m->a);
    fputs("\n", stdout);
    return;
  case OP_JOIN:
    out_num("    ; join ", m->a);
    out_num(" to ", m->fn);
    fputs("\n", stdout);
    return;
  case OP_CASES:
    out_num("    ; cases ", m->a);
    fputs("\n", stdout);
    return;
  case OP_CASEVAL:
    out_num("    ; case ", m->a);
    return;
  case OP_CASETO:
    out_num(" -> ", m->a);
    fputs("\n", stdout);
    return;
  }
  if (MOP_IS_CN(m->op)) {
    out_num("    CN ", MOP_CN_VALUE(m->op));
    fputs("\n", stdout);
    return;
  }
  fputs("    ", stdout);
  fputs(mnems[(m->op - OP_MINUS1) / 2], stdout);
  if (vol)
    fputs(".v", stdout);        /* volatile (tools/opstats.py reads LW) */
  switch (m->op) {
  case OP_ENTER:
    out_num(" ", m->a == FRAME_LATER ? frame : m->a);
    out_num(" ", m->fn);
    fputs("\n", stdout);
    return;
  case OP_LEAVE:
    out_num(" ", m->a == FRAME_LATER ? frame : m->a);
    fputs("\n", stdout);
    return;
  case OP_CB: case OP_CFFB: case OP_ANDI: case OP_ORI: case OP_ADDI:
  case OP_SUBI:
    out_num(" ", m->a);
    fputs("\n", stdout);
    return;
  case OP_LLA: case OP_LLB: case OP_LLW: case OP_DLB: case OP_DLW:
  case OP_SLB: case OP_SLW:
    out_num(" ", m->a);
    fputs("\n", stdout);
    return;
  case OP_CW: case OP_LA: case OP_CALL: case OP_LAB: case OP_LAW:
  case OP_SAB: case OP_SAW: case OP_DAB: case OP_DAW:
    list_word(m);
    break;
  case OP_BRNCH: case OP_BRFLS: case OP_BRTRU: case OP_BREQ:
  case OP_BRNE: case OP_SEL: case OP_BRGT: case OP_BRLT:
  case OP_INCBRLE: case OP_ADDBRLE: case OP_DECBRGE: case OP_SUBBRGE:
    out_num(" ", m->a);
    break;
  default:
    if (m->op >= OP_ADDLB && m->op <= OP_IDXAW) {
      if (m->op & 4)      /* the absolute-address form */
        list_word(m);
      else
        out_num(" ", m->a);
    } else if (m->fix == FIX_NONE && mop_const_value(m, &v))
      out_num(" ", v);
  }
  fputs("\n", stdout);
}

/* Print the listing of the function just ended: its name, then its
 * ops, with frame its frame size. */
static void list_fn(int frame)
{
  int i;
  if (nlisted++)
    fputs("\n", stdout);
  fputs("    ; ", stdout);
  fputs(func_name(fn), stdout);
  fputs("\n", stdout);
  for (i = 0; i < nlbuf; i++)
    list_op(&lbuf[i], frame);
  nlbuf = 0;
}
#endif

/* Stage 4's entry: encode one machine op into the function buffer. The
 * pseudo-ops start a function, or end it and write it out. */
void emit_put(Mop *m)
{
  int i, op;
#ifdef CCISH_LISTING
  if (opt_dump && m->op != OP_FUNC_BEGIN && m->op != OP_FUNC_END
      && nlbuf < NLBUF)
    lbuf[nlbuf++] = *m;
#endif
  m->op = MOP_PLAIN(m->op);     /* a volatile op's opcode is the plain one */
  switch (m->op) {
  case OP_FUNC_BEGIN:
    fn = m->fn;
    nfbuf = nrelocs = nframe = ntemp = temp_words = 0;
#ifdef CCISH_LISTING
    nlbuf = 0;
#endif
    for (i = 0; i < MAX_LABELS; i++) {
      label_word[i] = NO_LINK;
      label_placed[i] = 0;
    }
    return;
  case OP_FUNC_END:
#ifdef CCISH_LISTING
    if (opt_dump)
      list_fn(m->a);
#endif
    end_function(m->a);
    return;
  case OP_LABEL:
    patch_label(m->a);
    return;
  case OP_JOIN:
    join_labels(m->a, m->fn);
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
  op = m->op;
  /* ADDLB to IDXAW: those with 4 set (ADDAB, ADDAW, IDXAB, IDXAW) take
   * an address word, as LAW does; the others a local's byte, as LLW. */
  if (op >= OP_ADDLB && op <= OP_IDXAW)
    op = op & 4 ? OP_LAW : OP_LLW;
  switch (op) {
  case OP_CB: case OP_CFFB: case OP_ANDI: case OP_ORI: case OP_ADDI:
  case OP_SUBI:
    code_byte(m->a);
    break;
  case OP_LLA: case OP_LLB: case OP_LLW: case OP_DLB: case OP_DLW:
  case OP_SLB: case OP_SLW:
    frame_operand(m->a);
    break;
  case OP_CW: case OP_LA: case OP_CALL: case OP_LAB: case OP_LAW:
  case OP_SAB: case OP_SAW: case OP_DAB: case OP_DAW:
    /* A relocated word is written as 0, or for an extern global as
     * its addend; the final pass fills it in. */
    if (m->fix == FIX_DATA)
      add_reloc(REL_DATA, m->a);
    else if (m->fix == FIX_STR)
      add_reloc(REL_STR, m->a);
    else if (m->fix == FIX_FUNC)
      add_reloc(REL_FUNC, m->fn);
    else if (m->fix == FIX_DSYM || m->fix == FIX_HEAP)
      add_reloc(REL_DSYM, m->fn);
    else if (m->fix == FIX_HELPER)
      add_reloc(REL_HELPER, m->fn);
    code_word(m->fix == FIX_NONE || m->fix == FIX_DSYM ? m->a : 0);
    break;
  case OP_BRNCH: case OP_BRFLS: case OP_BRTRU: case OP_BREQ:
  case OP_BRNE: case OP_SEL: case OP_BRGT: case OP_BRLT: case OP_INCBRLE:
  case OP_ADDBRLE: case OP_DECBRGE: case OP_SUBBRGE:
    branch_word(m->a);
    break;
  case OP_ENTER:
    frame_byte(m->a);
    code_byte(m->fn);
    break;
  case OP_LEAVE:
    frame_byte(m->a);
    break;
  }
}

/* Close the output file, written as base + suffix, checking for
 * errors. */
static void close_out(const char *suffix)
{
  if (bclose(&out) != 0)
    fatal(187 /* "error writing" */, stream_name(base_name, suffix));
}


/* After an error: close the output file if it is open, writing out what
 * is buffered, so that a kept stream (-k) is complete up to the error,
 * and so that ProDOS can remove the file. */
void emit_abort(void)
{
  bclose(&out);
}

/* Close the stream, and start the symbol summary: its magic and
 * version. irin.c writes the rest (emit_byte()), from the IR file's. */
void emit_summary(void)
{
  close_out(SUFFIX_OBJ);
  open_out(base_name, SUFFIX_SYMS);
  bputc(&out, 'C');
  bputc(&out, 'I');
  bputc(&out, OBJ_VERSION);
}

/* Close the symbol summary. */
void emit_finish(void)
{
  close_out(SUFFIX_SYMS);
}
