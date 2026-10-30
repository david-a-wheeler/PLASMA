/*
 * The final pass: turn the compiler's stream file and symbol summary
 * (../OBJECT.md) into a PLASMA REL module (../../doc/Bytecodes.md), and
 * print the listing (-l), since only this pass knows the final
 * addresses.
 *
 * It uses none of the compiler's tables (fn_name[] and so on), only the
 * files, so it is a program of its own, ccrel (approach.md, "B3"), and
 * can become a linker for several files (issue 11).
 *
 * The files are read in order, with no seeking, and only one at a time
 * (approach.md 18.6 B6a):
 *   1. the symbol summary, so that every address can be computed;
 *   2. the header and dependencies are written; then the stream is read
 *      for its data records, with the data relocations applied (and
 *      kept for the RLD), and again for its strings, the string pool;
 *   3. the stream is read a third time for its functions, each copied
 *      with its relocations applied and kept for the RLD; then the init
 *      function is added;
 *   4. the RLD and ESD are written.
 */
#include <string.h>
#include "ccish.h"

typedef struct {
  char name[MAX_NAME + 1];
  int flags;          /* SYM_* */
  int nparams;        /* 255 if unknown */
  int code_off;       /* defined functions: offset in the code segment */
  int import_idx;     /* ESD index if imported, else -1 */
} Lsym;

static Lsym lsyms[MAX_FUNCS];
static int nlsyms;
static int data_size, strs_size, code_size;

/* Load addresses: of the data segment, the string pool after it, and
 * the code segment after that. */
static int data_base, strs_base, subseg;

typedef struct {
  int kind;       /* REL_DATA, REL_STR or REL_FUNC */
  int at;         /* load address of the word (in data or code) */
  int target;     /* data or string pool offset, or symbol index */
} Lreloc;

static Lreloc relocs[MAX_RELOCS];
static int nrelocs;

/* Code-segment offsets of the functions, in code order, for the RLD. */
static int func_at[MAX_FUNCS];
static int ndefs;

static unsigned char fbuf[MAX_FUNC_CODE];

/* Stop with an error: stream file what is cut short or disagrees with
 * the others. */
static void damaged(const char *what)
{
  fatal(188 /* "compiler stream file is damaged:" */, what);
}

/* ---------- Step 1: the symbol summary ---------- */

/* The file the final pass is reading (in) and the module it writes
 * (out). They are static, not locals, so that their 512-byte buffers
 * take no frame space (a frame holds 255 bytes), and so that
 * link_abort() can close them after an error. */
static Bfile in, out;

/* After an error: close whichever of the final pass's files are open
 * (ProDOS needs it). */
void link_abort(void)
{
  bclose(&in);
  bclose(&out);
}

/* Step 1: read the symbol summary into lsyms[], with the data and
 * code sizes. */
static void read_summary(const char *base)
{
  const char *name = stream_name(base, SUFFIX_SYMS);
  int i, n, j;

  if (!bopen(&in, name, 0))
    fatal(171 /* "cannot open" */, name);
  if (bgetc(&in) != 'C' || bgetc(&in) != 'I' || bgetc(&in) != OBJ_VERSION)
    damaged(name);
  data_size = bgetw(&in);
  strs_size = bgetw(&in);
  code_size = bgetw(&in);
  nlsyms = bgetw(&in);
  if (nlsyms > MAX_FUNCS)
    fatal(26 /* "too many functions" */, NULL);
  for (i = 0; i < nlsyms; i++) {
    Lsym *s = &lsyms[i];
    s->flags = bgetc(&in);
    s->nparams = bgetc(&in);
    s->code_off = bgetw(&in);
    n = bgetc(&in);
    if (n < 1 || n > MAX_NAME)
      damaged(name);
    for (j = 0; j < n; j++)
      s->name[j] = (char)bgetc(&in);
    s->name[n] = 0;
    s->import_idx = -1;
  }
  if (bgetc(&in) != EOF)
    damaged(name);
  bclose(&in);
}

/* ---------- Listing ---------- */

/* Operand formats for the disassembler. */
enum { F_NONE, F_BYTE, F_WORD, F_REL, F_ENTER, F_STR };

static const struct { const char *name; int fmt; } opinfo[0xC6 / 2] = {
  /* $00-$1E: CN 0..15 */
  {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE},
  {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE},
  {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE},
  {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE}, {"CN", F_NONE},
  /* $20 */
  {"MINUS1", F_NONE}, {"BREQ", F_REL}, {"BRNE", F_REL}, {"LA", F_WORD},
  {"LLA", F_BYTE}, {"CB", F_BYTE}, {"CW", F_WORD}, {"CS", F_STR},
  /* $30 */
  {"DROP", F_NONE}, {"DROP2", F_NONE}, {"DUP", F_NONE}, {"DIVMOD", F_NONE},
  {"ADDI", F_BYTE}, {"SUBI", F_BYTE}, {"ANDI", F_BYTE}, {"ORI", F_BYTE},
  /* $40 */
  {"ISEQ", F_NONE}, {"ISNE", F_NONE}, {"ISGT", F_NONE}, {"ISLT", F_NONE},
  {"ISGE", F_NONE}, {"ISLE", F_NONE}, {"BRFLS", F_REL}, {"BRTRU", F_REL},
  /* $50 */
  {"BRNCH", F_REL}, {"SEL", F_REL}, {"CALL", F_WORD}, {"ICAL", F_NONE},
  {"ENTER", F_ENTER}, {"LEAVE", F_BYTE}, {"RET", F_NONE}, {"CFFB", F_BYTE},
  /* $60 */
  {"LB", F_NONE}, {"LW", F_NONE}, {"LLB", F_BYTE}, {"LLW", F_BYTE},
  {"LAB", F_WORD}, {"LAW", F_WORD}, {"DLB", F_BYTE}, {"DLW", F_BYTE},
  /* $70 */
  {"SB", F_NONE}, {"SW", F_NONE}, {"SLB", F_BYTE}, {"SLW", F_BYTE},
  {"SAB", F_WORD}, {"SAW", F_WORD}, {"DAB", F_WORD}, {"DAW", F_WORD},
  /* $80 */
  {"NOT", F_NONE}, {"ADD", F_NONE}, {"SUB", F_NONE}, {"MUL", F_NONE},
  {"DIV", F_NONE}, {"MOD", F_NONE}, {"INCR", F_NONE}, {"DECR", F_NONE},
  /* $90 */
  {"NEG", F_NONE}, {"COMP", F_NONE}, {"AND", F_NONE}, {"IOR", F_NONE},
  {"XOR", F_NONE}, {"SHL", F_NONE}, {"SHR", F_NONE}, {"IDXW", F_NONE},
  /* $A0 */
  {"BRGT", F_REL}, {"BRLT", F_REL}, {"INCBRLE", F_REL}, {"ADDBRLE", F_REL},
  {"DECBRGE", F_REL}, {"SUBBRGE", F_REL}, {"BRAND", F_REL}, {"BROR", F_REL},
  /* $B0 */
  {"ADDLB", F_BYTE}, {"ADDLW", F_BYTE}, {"ADDAB", F_WORD}, {"ADDAW", F_WORD},
  {"IDXLB", F_BYTE}, {"IDXLW", F_BYTE}, {"IDXAB", F_WORD}, {"IDXAW", F_WORD},
  /* $C0 */
  {"NATV", F_NONE}, {"JUMPZ", F_WORD}, {"JUMP", F_WORD}
};

/* The comment for a relocated word at load address at; the
 * relocations of the function being listed start at index first. */
static const char *reloc_note(int at, int first)
{
  static char buf[MAX_NAME + 16];
  int i;
  for (i = first; i < nrelocs; i++)
    if (relocs[i].at == at) {
      if (relocs[i].kind == REL_DATA) {
        sprintf(buf, "  ; data+%d", relocs[i].target);
      } else if (relocs[i].kind == REL_STR) {
        sprintf(buf, "  ; string+%d", relocs[i].target);
      } else {
        Lsym *s = &lsyms[relocs[i].target];
        strcpy(buf, "  ; ");
        strcat(buf, s->name);
        if (!(s->flags & SYM_DEFINED))
          strcat(buf, " (import)");
      }
      return buf;
    }
  return "";
}

/* List the case table at fbuf offset pc (code-segment offset start +
 * pc): its count, then each case's value and where it goes. Returns its
 * size in bytes. */
static int list_table(int pc, int start)
{
  int n = fbuf[pc], i, at, w;
  printf("  %04X  %02X        CASES %d\n", subseg + start + pc, n, n);
  for (i = 0; i < n; i++) {
    at = pc + 1 + 4 * i;
    w = (short)(fbuf[at] | (fbuf[at + 1] << 8));
    printf("  %04X  %02X %02X     CASE %d\n", subseg + start + at,
           fbuf[at], fbuf[at + 1], w);
    at += 2;
    w = (short)(fbuf[at] | (fbuf[at + 1] << 8));
    printf("  %04X  %02X %02X     TO %+d  ; to %04X\n",
           subseg + start + at, fbuf[at], fbuf[at + 1], w,
           subseg + start + at + w);
  }
  return 1 + 4 * n;
}

/* List len bytes of code (in fbuf) that start at code-segment offset
 * start; addresses are shown as loaded at subseg. A SEL's case table is
 * data inside the code, so each SEL's target is noted, and the table is
 * listed as a table when the listing reaches it. */
static void list_code(const char *label, int len, int start, int first_reloc)
{
  int pc = 0, i, tables[MAX_NEST], ntables = 0;
  printf("%s:\n", label);
  while (pc < len) {
    int op = fbuf[pc], n = 1, fmt;
    for (i = 0; i < ntables && tables[i] != pc; i++)
      ;
    if (i < ntables) {
      tables[i] = tables[--ntables];
      pc += list_table(pc, start);
      continue;
    }
    if (op & 1 || op >= 0xC6) {
      printf("  %04X  %02X        ???\n", subseg + start + pc, op);
      pc++;
      continue;
    }
    fmt = opinfo[op / 2].fmt;
    if (fmt == F_BYTE)
      n = 2;
    else if (fmt == F_WORD || fmt == F_REL || fmt == F_ENTER)
      n = 3;
    else if (fmt == F_STR)
      n = 2 + fbuf[pc + 1];
    printf("  %04X  ", subseg + start + pc);
    for (i = 0; i < 3; i++)
      if (i < n)
        printf("%02X ", fbuf[pc + i]);
      else
        printf("   ");
    printf("%s", opinfo[op / 2].name);
    switch (fmt) {
    case F_NONE:
      if (op < 0x20)
        printf(" %d", op / 2);
      break;
    case F_BYTE:
      printf(" %d", fbuf[pc + 1]);
      break;
    case F_WORD:
      printf(" $%04X%s", fbuf[pc + 1] | (fbuf[pc + 2] << 8),
             reloc_note(subseg + start + pc + 1, first_reloc));
      break;
    case F_REL: {
      /* The offset is relative to the offset word itself. */
      int off = (short)(fbuf[pc + 1] | (fbuf[pc + 2] << 8));
      printf(" %+d  ; to %04X", off, subseg + start + pc + 1 + off);
      if (op == OP_SEL && ntables < MAX_NEST)
        tables[ntables++] = pc + 1 + off;
      break;
    }
    case F_ENTER:
      printf(" %d,%d", fbuf[pc + 1], fbuf[pc + 2]);
      break;
    case F_STR:
      printf(" (%d chars)", fbuf[pc + 1]);
      break;
    }
    printf("\n");
    pc += n;
  }
}

/* ---------- Steps 2-4: the module ---------- */

/* Write a name in DCI form: uppercase, high bit set on every character
 * but the last. PLASMA keeps 16 significant characters. */
static void out_dci(const char *name)
{
  int n = (int)strlen(name), i;
  if (n > 16)
    n = 16;
  for (i = 0; i < n; i++) {
    int c = name[i];
    if (c >= 'a' && c <= 'z')
      c -= 'a' - 'A';
    bputc(&out, i < n - 1 ? c | 0x80 : c);
  }
}

/* Keep a relocation of the word at load address at, for the RLD and
 * the listing. Returns the word's final value. A record read from
 * stream file name is checked first. */
static int add_reloc(int kind, int at, int target, const char *name)
{
  Lreloc *r = &relocs[nrelocs];
  if ((kind == REL_DATA && target > data_size)
      || (kind == REL_STR && target > strs_size)
      || (kind == REL_FUNC && target >= nlsyms)
      || (kind != REL_DATA && kind != REL_STR && kind != REL_FUNC))
    damaged(name);
  if (nrelocs >= MAX_RELOCS)
    fatal(189 /* "too many relocations" */, NULL);
  r->kind = kind;
  r->at = at;
  r->target = target;
  nrelocs++;
  if (kind == REL_DATA)
    return data_base + target;
  if (kind == REL_STR)
    return strs_base + target;
  if (lsyms[target].flags & SYM_DEFINED)
    return subseg + lsyms[target].code_off;
  return 0;       /* an import: the loader fills it in */
}

/* The tags each step reads from the stream; it skips the others. */
static const char data_tags[] = { REC_DATA, REC_DREL, 0 };
static const char str_tags[] = { REC_STR, 0 };
static const char func_tags[] = { REC_FUNC, 0 };

/* Open the stream file of output name base for reading, from its
 * start. */
static void open_obj(const char *base)
{
  const char *name = stream_name(base, SUFFIX_OBJ);
  if (!bopen(&in, name, 0))
    fatal(171 /* "cannot open" */, name);
}

/* Read past the rest of a record whose tag t has just been read. */
static void skip_record(int t)
{
  int n;
  if (t == REC_DATA) {
    if ((n = bgetc(&in)) == EOF)
      damaged(SUFFIX_OBJ);
    bskip(&in, n);
  } else if (t == REC_DREL) {
    bskip(&in, 3);
  } else if (t == REC_STR) {
    bskip(&in, bgetw(&in));
  } else if (t == REC_FUNC) {
    bskip(&in, 2);
    bskip(&in, bgetw(&in));
    if ((n = bgetw(&in)) > MAX_FUNC_RELOCS)
      damaged(SUFFIX_OBJ);
    bskip(&in, 5 * n);
  } else {
    damaged(SUFFIX_OBJ);
  }
}

/* Read the next record's tag that is one of want, skipping records with
 * other tags; or return EOF at the end of the stream. */
static int next_record(const char *want)
{
  int t;
  while ((t = bgetc(&in)) != EOF && (t == 0 || !strchr(want, t)))
    skip_record(t);
  return t;
}

/* Copy the next n bytes of the stream into the module. */
static void copy_bytes(int n)
{
  int c;
  while (n-- > 0) {
    if ((c = bgetc(&in)) == EOF)
      damaged(SUFFIX_OBJ);
    bputc(&out, c);
  }
}

/* Copy the data into the module: the stream's D records, with each R
 * record's word given its final value (an address, kept for the RLD). */
static void copy_data(const char *base)
{
  int at = 0, t, n, kind, target;
  open_obj(base);
  while ((t = next_record(data_tags)) != EOF) {
    if (t == REC_DATA) {
      if ((n = bgetc(&in)) == EOF || at + n > data_size)
        damaged(SUFFIX_OBJ);
      copy_bytes(n);
      at += n;
    } else {
      kind = bgetc(&in);
      target = bgetw(&in);
      if (at + 2 > data_size)
        damaged(SUFFIX_OBJ);
      bputw(&out, add_reloc(kind, data_base + at, target, SUFFIX_OBJ));
      at += 2;
    }
  }
  bclose(&in);
  if (at != data_size)
    damaged(SUFFIX_OBJ);
}

/* Copy the string pool into the module: each S record's string as
 * [len][chars][NUL]. A string longer than 255 gets a length byte of 0,
 * which is meaningless (pstr() rejects such a string). */
static void copy_strings(const char *base)
{
  int at = 0, n;
  open_obj(base);
  while (next_record(str_tags) != EOF) {
    n = bgetw(&in);
    if (n > strs_size - at - 2)
      damaged(SUFFIX_OBJ);
    bputc(&out, n > 255 ? 0 : n);
    copy_bytes(n);
    bputc(&out, 0);
    at += n + 2;
  }
  bclose(&in);
  if (at != strs_size)
    damaged(SUFFIX_OBJ);
}

/* Copy the code into the module, one F record (function) at a time. */
static void copy_code(const char *base)
{
  int at = 0, idx, len, n, i, first, c;

  open_obj(base);
  while (next_record(func_tags) != EOF) {
    idx = bgetw(&in);
    len = bgetw(&in);
    if (idx >= nlsyms || !(lsyms[idx].flags & SYM_DEFINED)
        || lsyms[idx].code_off != at || len > MAX_FUNC_CODE
        || ndefs >= MAX_FUNCS)
      damaged(SUFFIX_OBJ);
    for (i = 0; i < len; i++) {
      if ((c = bgetc(&in)) == EOF)
        damaged(SUFFIX_OBJ);
      fbuf[i] = (unsigned char)c;
    }
    first = nrelocs;
    n = bgetw(&in);
    for (i = 0; i < n; i++) {
      int kind = bgetc(&in), off = bgetw(&in), target = bgetw(&in), v;
      if (off < 0 || off > len - 2)
        damaged(SUFFIX_OBJ);
      v = add_reloc(kind, subseg + at + off, target, SUFFIX_OBJ);
      fbuf[off] = (unsigned char)v;
      fbuf[off + 1] = (unsigned char)(v >> 8);
    }
    if (opt_list)
      list_code(lsyms[idx].name, len, at, first);
    bwrite(&out, fbuf, len);
    func_at[ndefs++] = at;
    at += len;
  }
  bclose(&in);
  if (at != code_size)
    damaged(SUFFIX_OBJ);
}

/* The init function runs when the module loads: call main, discard its
 * result, and return 0. A positive init result would be read as "keep
 * the module" flags. Init must be the last bytecode. */
static int init_code(int main_idx, int init_off)
{
  int n = 0, v = subseg + lsyms[main_idx].code_off;
  add_reloc(REL_FUNC, subseg + init_off + 1, main_idx, "init");
  fbuf[n++] = OP_CALL;
  fbuf[n++] = (unsigned char)v;
  fbuf[n++] = (unsigned char)(v >> 8);
  if (lsyms[main_idx].flags & SYM_RETURNS)
    fbuf[n++] = OP_DROP;
  fbuf[n++] = OP_CN;
  fbuf[n++] = OP_RET;
  return n;
}

/* Bytes in the init function that init_code builds, or 0 if there is
 * no main. */
static int init_size(int main_idx)
{
  return main_idx < 0 ? 0 : 6 - !(lsyms[main_idx].flags & SYM_RETURNS);
}

/* Run the final pass: read the stream files for output name base, and
 * write the REL module to path. */
void link_module(const char *base, const char *path)
{
  static const char dep[] = "CMDSYS";
  int module_base = 0x1000, hdr = 10;
  int nimports = 0, main_idx = -1, init_off;
  int deplen, defcnt = 0, total, i;

  /* Step 1. */
  read_summary(base);
  init_off = code_size;
  for (i = 0; i < nlsyms; i++) {
    Lsym *s = &lsyms[i];
    if (s->flags & SYM_DEFINED) {
      defcnt++;
      if (!strcmp(s->name, "main"))
        main_idx = i;
    } else if (s->flags & SYM_CALLED) {
      if (nimports >= 255)
        fatal(190 /* "too many imported functions" */, NULL);
      s->import_idx = nimports++;
    }
  }

  deplen = nimports ? (int)sizeof(dep) - 1 + 1 : 1;   /* name(s) + $00 */
  /* The data segment holds the globals, then the string pool. One pad
   * byte between it and the bytecode keeps data from ever sharing an
   * address with the first function. */
  data_base = module_base + hdr + deplen;
  strs_base = data_base + data_size;
  subseg = strs_base + strs_size + 1;
  total = code_size + init_size(main_idx);
  if (subseg + total > 0xFFFF)
    fatal(191 /* "module too large" */, NULL);

  /* Step 2. */
  if (!bopen(&out, path, 1))
    fatal(192 /* "cannot create output file" */, path);
  bputw(&out, subseg - module_base + total);
  bputw(&out, 0x6502);                    /* magic */
  bputw(&out, 0);                         /* system flags */
  bputw(&out, subseg);
  bputw(&out, defcnt + (main_idx >= 0));
  bputw(&out, main_idx >= 0 ? subseg + init_off : 0);
  if (nimports)
    out_dci(dep);
  bputc(&out, 0);                         /* end of dependencies */

  copy_data(base);
  copy_strings(base);
  bputc(&out, 0);                         /* pad */

  /* Step 3. */
  copy_code(base);
  if (main_idx >= 0) {
    int first = nrelocs, n = init_code(main_idx, init_off);
    if (opt_list)
      list_code("(init)", n, init_off, first);
    bwrite(&out, fbuf, n);
  }

  /* Step 4. RLD: function entries first (in code order, init
   * excluded), then the relocations. */
  for (i = 0; i < ndefs; i++) {
    bputc(&out, 0x02);
    bputw(&out, subseg + func_at[i]);
    bputc(&out, 0);
  }
  for (i = 0; i < nrelocs; i++) {
    Lreloc *r = &relocs[i];
    int at = r->at - module_base;
    if (r->kind == REL_FUNC && lsyms[r->target].import_idx >= 0) {
      bputc(&out, 0x91);
      bputw(&out, at);
      bputc(&out, lsyms[r->target].import_idx);
    } else {
      bputc(&out, 0x81);
      bputw(&out, at);
      bputc(&out, 0);
    }
  }
  bputc(&out, 0);

  /* ESD: imports. */
  for (i = 0; i < nlsyms; i++)
    if (lsyms[i].import_idx >= 0) {
      out_dci(lsyms[i].name);
      bputc(&out, 0x10);
      bputw(&out, lsyms[i].import_idx);
    }
  bputc(&out, 0);

  if (bclose(&out) != 0)
    fatal(193 /* "error writing output file" */, path);
}
