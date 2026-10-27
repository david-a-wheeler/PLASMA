/*
 * Stage 4: the emitter. Encodes machine ops into bytes, keeps the
 * data segment, records relocations, and writes the REL module.
 * The module format is described in ../../doc/Bytecodes.md.
 */
#include <string.h>
#include "ccish.h"

static unsigned char code[MAX_CODE];
static int ncode;
static unsigned char data[MAX_DATA];
static int ndata;

typedef struct {
    int at;         /* code offset of the operand word */
    int kind;       /* FIX_DATA or FIX_FUNC */
    int target;     /* FIX_DATA: data offset */
    Sym *sym;       /* FIX_FUNC: function */
} Fixup;

static Fixup fixups[MAX_FIXUPS];
static int nfixups;

/* Frame-size bytes of the current function, filled in at its end. */
static int frame_at[MAX_FIXUPS];
static int nframe;

static void code_byte(int v)
{
    if (ncode >= MAX_CODE)
        fatal("too much code", NULL);
    code[ncode++] = (unsigned char)v;
}

static void code_word(int v)
{
    code_byte(v & 0xFF);
    code_byte((v >> 8) & 0xFF);
}

static void add_fixup(int kind, int target, Sym *sym)
{
    if (nfixups >= MAX_FIXUPS)
        fatal("too many relocations", NULL);
    fixups[nfixups].at = ncode;
    fixups[nfixups].kind = kind;
    fixups[nfixups].target = target;
    fixups[nfixups].sym = sym;
    nfixups++;
}

static void frame_byte(int v)
{
    if (v == FRAME_LATER) {
        if (nframe >= MAX_FIXUPS)
            fatal("too many returns in one function", NULL);
        frame_at[nframe++] = ncode;
        v = 0;
    }
    code_byte(v);
}

int data_string(const char *s, int len)
{
    int first;
    if (ndata + len + 2 > MAX_DATA)
        fatal("too much data", NULL);
    /* [len][chars][NUL]: the C pointer is to the first character, and
     * the byte before it makes it a PLASMA string as well. A literal
     * longer than 255 has a meaningless length byte (pstr() rejects it). */
    data[ndata++] = (unsigned char)(len > 255 ? 0 : len);
    first = ndata;
    memcpy(data + ndata, s, len);
    ndata += len;
    data[ndata++] = 0;
    return first;
}

void emit_put(Mop *m)
{
    int i;
    switch (m->op) {
    case OP_FUNC_BEGIN:
        m->sym->code_off = ncode;
        nframe = 0;
        return;
    case OP_FUNC_END:
        if (m->a > 255)
            error("too many parameters and locals in", m->sym->name);
        for (i = 0; i < nframe; i++)
            code[frame_at[i]] = (unsigned char)m->a;
        nframe = 0;
        return;
    }
    code_byte(m->op);
    switch (m->op) {
    case OP_CB: case OP_CFFB: case OP_ANDI:
    case OP_LLW: case OP_DLW: case OP_SLW:
        code_byte(m->a);
        break;
    case OP_CW:
        code_word(m->a);
        break;
    case OP_LA:
        add_fixup(FIX_DATA, m->a, NULL);
        code_word(0);
        break;
    case OP_CALL:
        add_fixup(FIX_FUNC, 0, m->sym);
        code_word(0);
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

/* ---------- Listing ---------- */

/* Operand formats for the disassembler. */
enum { F_NONE, F_BYTE, F_WORD, F_REL, F_ENTER, F_STR };

static const struct { const char *name; int fmt; } opinfo[0xC6 / 2] = {
    /* $00-$1E: CN 0..15, filled in by name below */
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

static const char *fixup_note(int at)
{
    static char buf[MAX_NAME + 16];
    int i;
    for (i = 0; i < nfixups; i++)
        if (fixups[i].at == at) {
            if (fixups[i].kind == FIX_DATA) {
                sprintf(buf, "  ; data+%d", fixups[i].target);
            } else {
                strcpy(buf, "  ; ");
                strcat(buf, fixups[i].sym->name);
                if (!fixups[i].sym->defined)
                    strcat(buf, " (import)");
            }
            return buf;
        }
    return "";
}

static void list_code(int subseg, int init_off)
{
    int pc = 0, i;
    while (pc < ncode) {
        int op = code[pc], len = 1, fmt;
        for (i = 0; i < nsyms; i++)
            if (syms[i].defined && syms[i].code_off == pc)
                printf("%s:\n", syms[i].name);
        if (pc == init_off)
            printf("(init):\n");
        if (op & 1 || op >= 0xC6) {
            printf("  %04X  %02X        ???\n", subseg + pc, op);
            pc++;
            continue;
        }
        fmt = opinfo[op / 2].fmt;
        if (fmt == F_BYTE)
            len = 2;
        else if (fmt == F_WORD || fmt == F_REL || fmt == F_ENTER)
            len = 3;
        else if (fmt == F_STR)
            len = 2 + code[pc + 1];
        printf("  %04X  ", subseg + pc);
        for (i = 0; i < 3; i++)
            if (i < len)
                printf("%02X ", code[pc + i]);
            else
                printf("   ");
        printf("%s", opinfo[op / 2].name);
        switch (fmt) {
        case F_NONE:
            if (op < 0x20)
                printf(" %d", op / 2);
            break;
        case F_BYTE:
            printf(" %d", code[pc + 1]);
            break;
        case F_WORD:
            printf(" $%04X%s", code[pc + 1] | (code[pc + 2] << 8),
                   fixup_note(pc + 1));
            break;
        case F_REL:
            printf(" %+d", (short)(code[pc + 1] | (code[pc + 2] << 8)));
            break;
        case F_ENTER:
            printf(" %d,%d", code[pc + 1], code[pc + 2]);
            break;
        case F_STR:
            printf(" (%d chars)", code[pc + 1]);
            break;
        }
        printf("\n");
        pc += len;
    }
}

/* ---------- Module writer ---------- */

static FILE *out;

static void out_byte(int v)
{
    putc(v & 0xFF, out);
}

static void out_word(int v)
{
    out_byte(v);
    out_byte(v >> 8);
}

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
        out_byte(i < n - 1 ? c | 0x80 : c);
    }
}

void write_module(const char *path)
{
    static const char dep[] = "CMDSYS";
    int base = 0x1000, hdr = 10;
    int nimports = 0, has_init = 0, init_off = -1;
    int deplen, data_base, subseg, defcnt = 0, i, j;
    Sym *main_fn = NULL;
    Mop m;

    /* The init function runs when the module loads: call main, discard
     * its result, and return 0. A positive init result would be read as
     * "keep the module" flags. Init must be the last bytecode. */
    for (i = 0; i < nsyms; i++)
        if (syms[i].defined && !strcmp(syms[i].name, "main"))
            main_fn = &syms[i];
    if (main_fn) {
        has_init = 1;
        init_off = ncode;
        memset(&m, 0, sizeof(m));
        m.op = OP_CALL;
        m.fix = FIX_FUNC;
        m.sym = main_fn;
        emit_put(&m);
        if (main_fn->returns_value) {
            m.op = OP_DROP;
            emit_put(&m);
        }
        m.op = OP_CN;
        emit_put(&m);
        m.op = OP_RET;
        emit_put(&m);
    }

    for (i = 0; i < nsyms; i++) {
        if (syms[i].defined)
            defcnt++;
        else if (syms[i].called) {
            if (nimports >= 255)
                fatal("too many imported functions", NULL);
            syms[i].import_idx = nimports++;
        }
    }
    defcnt += has_init;

    deplen = nimports ? (int)sizeof(dep) - 1 + 1 : 1;   /* name(s) + $00 */
    data_base = base + hdr + deplen;
    /* One pad byte between data and bytecode, so that data can never
     * share an address with the first function. */
    subseg = data_base + ndata + 1;
    if (subseg + ncode > 0xFFFF)
        fatal("module too large", NULL);

    /* Resolve relocations now that the layout is known. */
    for (i = 0; i < nfixups; i++) {
        Fixup *f = &fixups[i];
        int v = 0;
        if (f->kind == FIX_DATA)
            v = data_base + f->target;
        else if (f->sym->defined)
            v = subseg + f->sym->code_off;
        code[f->at] = (unsigned char)v;
        code[f->at + 1] = (unsigned char)(v >> 8);
    }

    if (opt_list)
        list_code(subseg, init_off);

    out = fopen(path, "wb");
    if (!out)
        fatal("cannot create output file", path);

    out_word(hdr + deplen + ndata + 1 + ncode);
    out_word(0x6502);                       /* magic */
    out_word(0);                            /* system flags */
    out_word(subseg);
    out_word(defcnt);
    out_word(has_init ? subseg + init_off : 0);
    if (nimports)
        out_dci(dep);
    out_byte(0);                            /* end of dependencies */
    for (i = 0; i < ndata; i++)
        out_byte(data[i]);
    out_byte(0);                            /* pad */
    for (i = 0; i < ncode; i++)
        out_byte(code[i]);

    /* RLD: function entries first (in code order, init excluded), then
     * the fixups. */
    for (j = 0; j < ncode; j++)
        for (i = 0; i < nsyms; i++)
            if (syms[i].defined && syms[i].code_off == j) {
                out_byte(0x02);
                out_word(subseg + j);
                out_byte(0);
            }
    for (i = 0; i < nfixups; i++) {
        Fixup *f = &fixups[i];
        int at = subseg - base + f->at;
        if (f->kind == FIX_FUNC && !f->sym->defined) {
            out_byte(0x91);
            out_word(at);
            out_byte(f->sym->import_idx);
        } else {
            out_byte(0x81);
            out_word(at);
            out_byte(0);
        }
    }
    out_byte(0);

    /* ESD: imports. */
    for (i = 0; i < nsyms; i++)
        if (syms[i].import_idx >= 0) {
            out_dci(syms[i].name);
            out_byte(0x10);
            out_word(syms[i].import_idx);
        }
    out_byte(0);

    if (fclose(out) != 0)
        fatal("error writing output file", path);
}
