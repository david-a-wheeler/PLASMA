/*
 * The final pass: turn the compiler's three stream files (../OBJECT.md)
 * into a PLASMA REL module (../../doc/Bytecodes.md), and print the
 * listing (-l), since only this pass knows the final addresses.
 *
 * It uses none of the compiler's tables (syms[] and so on), only the
 * streams, so it can become a separate program later: on an Apple II,
 * a module loaded after the compiler has finished (approach.md 18.1),
 * and a linker for several files (issue 11).
 *
 * Each stream is read once, in order, with no seeking:
 *   1. the symbol summary, so that every address can be computed;
 *   2. the header and dependencies are written, then the data stream is
 *      copied;
 *   3. the code stream is copied one function at a time, with each
 *      function's relocations applied and kept for the RLD; then the
 *      init function is added;
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

static Lsym lsyms[MAX_SYMS];
static int nlsyms;
static int data_size, code_size;

typedef struct {
    int kind;       /* REL_DATA or REL_FUNC */
    int at;         /* offset of the word in the code segment */
    int target;     /* data offset, or symbol index */
} Lreloc;

static Lreloc relocs[MAX_RELOCS];
static int nrelocs;

/* Code-segment offsets of the functions, in code order, for the RLD. */
static int func_at[MAX_SYMS];
static int nfuncs;

static unsigned char fbuf[MAX_FUNC_CODE];

static void damaged(const char *what)
{
    fatal("compiler stream file is damaged:", what);
}

/* ---------- Step 1: the symbol summary ---------- */

static void read_summary(const char *base)
{
    Bfile in;
    const char *name = stream_name(base, SUFFIX_SYMS);
    int i, n, j;

    if (!bopen(&in, name, 0))
        fatal("cannot open", name);
    if (bgetc(&in) != 'C' || bgetc(&in) != 'I' || bgetc(&in) != 1)
        damaged(name);
    data_size = bgetw(&in);
    code_size = bgetw(&in);
    nlsyms = bgetw(&in);
    if (nlsyms > MAX_SYMS)
        fatal("too many functions", NULL);
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

/* The comment for a relocated word at code-segment offset at; the
 * relocations of the function being listed start at index first. */
static const char *reloc_note(int at, int first)
{
    static char buf[MAX_NAME + 16];
    int i;
    for (i = first; i < nrelocs; i++)
        if (relocs[i].at == at) {
            if (relocs[i].kind == REL_DATA) {
                sprintf(buf, "  ; data+%d", relocs[i].target);
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

/* List len bytes of code (in fbuf) that start at code-segment offset
 * start; addresses are shown as loaded at subseg. */
static void list_code(const char *label, int len, int start, int subseg,
                      int first_reloc)
{
    int pc = 0, i;
    printf("%s:\n", label);
    while (pc < len) {
        int op = fbuf[pc], n = 1, fmt;
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
                   reloc_note(start + pc + 1, first_reloc));
            break;
        case F_REL:
            printf(" %+d", (short)(fbuf[pc + 1] | (fbuf[pc + 2] << 8)));
            break;
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

static Bfile out;

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

static void add_reloc(int kind, int at, int target)
{
    if (nrelocs >= MAX_RELOCS)
        fatal("too many relocations", NULL);
    relocs[nrelocs].kind = kind;
    relocs[nrelocs].at = at;
    relocs[nrelocs].target = target;
    nrelocs++;
}

/* The final value of a relocated word. */
static int reloc_value(Lreloc *r, int data_base, int subseg)
{
    if (r->kind == REL_DATA)
        return data_base + r->target;
    if (lsyms[r->target].flags & SYM_DEFINED)
        return subseg + lsyms[r->target].code_off;
    return 0;       /* an import: the loader fills it in */
}

/* Copy the code stream into the module, one function at a time. */
static void copy_code(const char *base, int data_base, int subseg)
{
    Bfile in;
    const char *name = stream_name(base, SUFFIX_CODE);
    int at = 0, idx, len, n, i, first, c;

    if (!bopen(&in, name, 0))
        fatal("cannot open", name);
    while ((c = bgetc(&in)) != EOF) {
        bungetc(&in, c);
        idx = bgetw(&in);
        len = bgetw(&in);
        if (idx >= nlsyms || !(lsyms[idx].flags & SYM_DEFINED)
            || lsyms[idx].code_off != at || len > MAX_FUNC_CODE
            || nfuncs >= MAX_SYMS)
            damaged(name);
        for (i = 0; i < len; i++) {
            if ((c = bgetc(&in)) == EOF)
                damaged(name);
            fbuf[i] = (unsigned char)c;
        }
        first = nrelocs;
        n = bgetw(&in);
        for (i = 0; i < n; i++) {
            int kind = bgetc(&in), off = bgetw(&in), target = bgetw(&in), v;
            if ((kind != REL_DATA && kind != REL_FUNC) || off > len - 2
                || (kind == REL_FUNC && target >= nlsyms))
                damaged(name);
            add_reloc(kind, at + off, target);
            v = reloc_value(&relocs[nrelocs - 1], data_base, subseg);
            fbuf[off] = (unsigned char)v;
            fbuf[off + 1] = (unsigned char)(v >> 8);
        }
        if (opt_list)
            list_code(lsyms[idx].name, len, at, subseg, first);
        bwrite(&out, fbuf, len);
        func_at[nfuncs++] = at;
        at += len;
    }
    if (at != code_size)
        damaged(name);
    bclose(&in);
}

/* The init function runs when the module loads: call main, discard its
 * result, and return 0. A positive init result would be read as "keep
 * the module" flags. Init must be the last bytecode. */
static int init_code(int main_idx, int init_off, int subseg)
{
    int n = 0, v = subseg + lsyms[main_idx].code_off;
    add_reloc(REL_FUNC, init_off + 1, main_idx);
    fbuf[n++] = OP_CALL;
    fbuf[n++] = (unsigned char)v;
    fbuf[n++] = (unsigned char)(v >> 8);
    if (lsyms[main_idx].flags & SYM_RETURNS)
        fbuf[n++] = OP_DROP;
    fbuf[n++] = OP_CN;
    fbuf[n++] = OP_RET;
    return n;
}

static int init_size(int main_idx)
{
    return main_idx < 0 ? 0 : 6 - !(lsyms[main_idx].flags & SYM_RETURNS);
}

void link_module(const char *base, const char *path)
{
    static const char dep[] = "CMDSYS";
    int module_base = 0x1000, hdr = 10;
    int nimports = 0, main_idx = -1, init_off;
    int deplen, data_base, subseg, defcnt = 0, total, i, c;
    Bfile in;
    const char *name;

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
                fatal("too many imported functions", NULL);
            s->import_idx = nimports++;
        }
    }

    deplen = nimports ? (int)sizeof(dep) - 1 + 1 : 1;   /* name(s) + $00 */
    data_base = module_base + hdr + deplen;
    /* One pad byte between data and bytecode, so that data can never
     * share an address with the first function. */
    subseg = data_base + data_size + 1;
    total = code_size + init_size(main_idx);
    if (subseg + total > 0xFFFF)
        fatal("module too large", NULL);

    /* Step 2. */
    if (!bopen(&out, path, 1))
        fatal("cannot create output file", path);
    bputw(&out, hdr + deplen + data_size + 1 + total);
    bputw(&out, 0x6502);                    /* magic */
    bputw(&out, 0);                         /* system flags */
    bputw(&out, subseg);
    bputw(&out, defcnt + (main_idx >= 0));
    bputw(&out, main_idx >= 0 ? subseg + init_off : 0);
    if (nimports)
        out_dci(dep);
    bputc(&out, 0);                         /* end of dependencies */

    name = stream_name(base, SUFFIX_DATA);
    if (!bopen(&in, name, 0))
        fatal("cannot open", name);
    for (i = 0; (c = bgetc(&in)) != EOF; i++)
        bputc(&out, c);
    bclose(&in);
    if (i != data_size)
        damaged(name);
    bputc(&out, 0);                         /* pad */

    /* Step 3. */
    copy_code(base, data_base, subseg);
    if (main_idx >= 0) {
        int first = nrelocs, n = init_code(main_idx, init_off, subseg);
        if (opt_list)
            list_code("(init)", n, init_off, subseg, first);
        bwrite(&out, fbuf, n);
    }

    /* Step 4. RLD: function entries first (in code order, init
     * excluded), then the relocations. */
    for (i = 0; i < nfuncs; i++) {
        bputc(&out, 0x02);
        bputw(&out, subseg + func_at[i]);
        bputc(&out, 0);
    }
    for (i = 0; i < nrelocs; i++) {
        Lreloc *r = &relocs[i];
        int at = subseg - module_base + r->at;
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
        fatal("error writing output file", path);
}
