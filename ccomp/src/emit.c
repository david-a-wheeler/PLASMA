/*
 * Stage 4: the emitter. Encodes machine ops into bytes and writes the
 * compiler's output as three stream files (approach.md 18.1; formats in
 * ../OBJECT.md):
 *
 *   .data  the data segment's bytes, in order (string literals so far)
 *   .code  for each function: its bytecode, then its relocation records
 *   .syms  the symbol summary, written when compilation ends
 *
 * Only one function's bytecode is in memory at a time, in a function
 * buffer, so that its frame size (and, later, its branch targets) can be
 * patched before it is written. Memory use doesn't depend on the size of
 * the program. The final pass (link.c) turns the streams into a module.
 */
#include <string.h>
#include "ccish.h"

static Bfile data_out, code_out;
static const char *base_name;

static int ndata;               /* bytes written to the data stream */
static int ncode;               /* bytes written to the code stream so far */

/* The function being built. */
static Sym *fn;
static unsigned char fbuf[MAX_FUNC_CODE];
static int nfbuf;

typedef struct {
    int kind;       /* REL_DATA or REL_FUNC */
    int at;         /* offset of the operand word in fbuf */
    int target;     /* REL_DATA: data offset; REL_FUNC: symbol index */
} Reloc;

static Reloc relocs[MAX_FUNC_RELOCS];
static int nrelocs;

/* Frame-size bytes of the current function, filled in at its end. */
static int frame_at[MAX_RETURNS];
static int nframe;

/* Create the data and code stream files for output name base. */
void emit_open(const char *base)
{
    base_name = base;
    if (!bopen(&data_out, stream_name(base, SUFFIX_DATA), 1))
        fatal("cannot create", stream_name(base, SUFFIX_DATA));
    if (!bopen(&code_out, stream_name(base, SUFFIX_CODE), 1))
        fatal("cannot create", stream_name(base, SUFFIX_CODE));
}

/* Append one byte to the function buffer. */
static void code_byte(int v)
{
    if (nfbuf >= MAX_FUNC_CODE)
        error("function too large:", fn->name);
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
        error("too many calls and data references in", fn->name);
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
            error("too many returns in", fn->name);
        frame_at[nframe++] = nfbuf;
        v = 0;
    }
    code_byte(v);
}

/* Write a string literal of len bytes to the data stream. Returns the
 * data offset of its first character. */
int data_string(const char *s, int len)
{
    int first, i;
    /* Keep data offsets well inside 16 bits; the final pass checks
     * that the whole module fits. */
    if (ndata > 0x7FFF - len - 2)
        fatal("too much data", NULL);
    /* [len][chars][NUL]: the C pointer is to the first character, and
     * the byte before it makes it a PLASMA string as well. A literal
     * longer than 255 has a meaningless length byte (pstr() rejects it). */
    bputc(&data_out, len > 255 ? 0 : len);
    first = ndata + 1;
    for (i = 0; i < len; i++)
        bputc(&data_out, s[i]);
    bputc(&data_out, 0);
    ndata += len + 2;
    return first;
}

/* Write the finished function to the code stream. */
static void end_function(int frame_size)
{
    int i;
    if (frame_size > 255)
        error("too many parameters and locals in", fn->name);
    for (i = 0; i < nframe; i++)
        fbuf[frame_at[i]] = (unsigned char)frame_size;
    if (ncode > 0x7FFF - nfbuf)
        fatal("too much code", NULL);
    fn->code_off = ncode;
    bputw(&code_out, (int)(fn - syms));
    bputw(&code_out, nfbuf);
    bwrite(&code_out, fbuf, nfbuf);
    bputw(&code_out, nrelocs);
    for (i = 0; i < nrelocs; i++) {
        bputc(&code_out, relocs[i].kind);
        bputw(&code_out, relocs[i].at);
        bputw(&code_out, relocs[i].target);
    }
    ncode += nfbuf;
    fn = NULL;
}

/* Stage 4's entry: encode one machine op into the function buffer. The
 * pseudo-ops start a function, or end it and write it out. */
void emit_put(Mop *m)
{
    switch (m->op) {
    case OP_FUNC_BEGIN:
        fn = m->sym;
        nfbuf = nrelocs = nframe = 0;
        return;
    case OP_FUNC_END:
        end_function(m->a);
        return;
    }
    code_byte(m->op);
    switch (m->op) {
    case OP_CB: case OP_CFFB: case OP_ANDI:
    case OP_LLW: case OP_DLW: case OP_SLW:
        code_byte(m->a);
        break;
    case OP_CW: case OP_LA: case OP_CALL:
        /* A relocated word is written as 0; the final pass fills it in. */
        if (m->fix == FIX_DATA)
            add_reloc(REL_DATA, m->a);
        else if (m->fix == FIX_FUNC)
            add_reloc(REL_FUNC, (int)(m->sym - syms));
        code_word(m->fix == FIX_NONE ? m->a : 0);
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

/* Close the data and code streams and write the symbol summary. */
void emit_finish(void)
{
    Bfile out;
    int i, n;
    const char *name;

    if (bclose(&data_out) != 0)
        fatal("error writing", stream_name(base_name, SUFFIX_DATA));
    if (bclose(&code_out) != 0)
        fatal("error writing", stream_name(base_name, SUFFIX_CODE));

    /* stream_name() uses one static buffer, so get this name last. */
    name = stream_name(base_name, SUFFIX_SYMS);
    if (!bopen(&out, name, 1))
        fatal("cannot create", name);
    bputc(&out, 'C');
    bputc(&out, 'I');
    bputc(&out, 1);                         /* format version */
    bputw(&out, ndata);
    bputw(&out, ncode);
    bputw(&out, nsyms);
    for (i = 0; i < nsyms; i++) {
        Sym *s = &syms[i];
        bputc(&out, (s->defined ? SYM_DEFINED : 0)
                    | (s->called ? SYM_CALLED : 0)
                    | (s->returns_value ? SYM_RETURNS : 0));
        bputc(&out, s->nparams < 0 ? 255 : s->nparams);
        bputw(&out, s->defined ? s->code_off : 0);
        n = (int)strlen(s->name);
        bputc(&out, n);
        bwrite(&out, (const unsigned char *)s->name, n);
    }
    if (bclose(&out) != 0)
        fatal("error writing", name);
}
