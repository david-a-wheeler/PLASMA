/*
 * ccish - a compiler for "C-ish", a growing subset of C, that emits
 * PLASMA VM modules. See ../approach.md for the design and
 * ../../doc/Bytecodes.md for the target format.
 *
 * The compiler is a pipeline of four stages that pass a stream of ops
 * along, with no syntax tree:
 *
 *   parse.c  stage 1: C source -> IR ops        (ir_put)
 *   lower.c  stage 2: IR ops -> machine ops     (peep_put)
 *   peep.c   stage 3: peephole optimization     (emit_put)
 *   emit.c   stage 4: encode bytes, one function at a time, and write
 *            them to the stream files
 *
 * Then a separate final pass, link.c, reads only the stream files and
 * writes the REL module (approach.md 18.1; formats in ../OBJECT.md).
 * It uses none of the compiler's tables, so it can become a separate
 * program (and a linker) later.
 *
 * Fixed-size tables are used on purpose: the compiler is meant to run
 * on a 64K machine eventually, so its limits should be visible.
 */
#ifndef CCISH_H
#define CCISH_H

#include <stdio.h>

/* ---------- Limits ---------- */

#define MAX_NAME     31     /* significant characters in an identifier */
#define MAX_STR      512    /* bytes in one string literal */
#define MAX_SYMS     256
#define MAX_PARAMS   16
#define MAX_LOCALS   128    /* parameters and locals visible at once */
#define MAX_FUNC_CODE 4096  /* bytecode in one function */
#define MAX_FUNC_RELOCS 256 /* relocations in one function */
#define MAX_RETURNS  128    /* frame-size bytes (ENTER, LEAVEs) in one function */
#define MAX_RELOCS   2048   /* final pass: relocations in the module */
#define MAX_NEST     32     /* nesting of parentheses and blocks */
#define BLOCK        512    /* file I/O buffer size: one ProDOS block */

/* ---------- Diagnostics (main.c) ---------- */

extern const char *src_name;
extern int src_line;
extern int opt_list;        /* -l: print a listing of the generated code */
extern int opt_ir;          /* -i: print the IR stream */

void error(const char *msg, const char *detail);
void fatal(const char *msg, const char *detail);

/* ---------- Block-buffered file I/O (bio.c) ---------- */

typedef struct {
    FILE *f;
    int writing;
    int pos, len;       /* next byte in buf; bytes in buf when reading */
    int error;          /* a write failed */
    unsigned char buf[BLOCK];
} Bfile;

int  bopen(Bfile *b, const char *path, int writing);
int  bgetc(Bfile *b);
void bungetc(Bfile *b, int c);
int  bgetw(Bfile *b);
void bputc(Bfile *b, int c);
void bputw(Bfile *b, int v);
void bwrite(Bfile *b, const unsigned char *p, int n);
int  bclose(Bfile *b);

/* ---------- Lexer (lex.c) ---------- */

/* Single-character tokens are their character code; others start at 256. */
enum {
    T_EOF = 0,
    T_ID = 256, T_NUM, T_STR,
    /* keywords supported now */
    T_INT, T_VOID, T_RETURN,
    /* any other C keyword: reported as "not supported yet" */
    T_KEYWORD,
    /* multi-character operators (not all are supported by the parser yet) */
    T_EQ, T_NE, T_LE, T_GE, T_SHL, T_SHR, T_ANDAND, T_OROR, T_INC, T_DEC,
    T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ, T_MODEQ, T_ANDEQ, T_OREQ, T_XOREQ,
    T_SHLEQ, T_SHREQ, T_ARROW, T_ELLIPSIS
};

/* The current token. */
extern int tok;
extern int tok_val;                 /* T_NUM value */
extern char tok_text[MAX_STR + 1];  /* T_ID / T_KEYWORD name, T_STR bytes */
extern int tok_len;                 /* T_STR length, not counting a NUL */

void lex_open(Bfile *f);
void next(void);                    /* advance to the next token */
const char *tok_desc(void);         /* the current token, for messages */

/* ---------- Symbols (parse.c) ---------- */

enum { S_FUNC, S_LOCAL };

typedef struct Sym Sym;
struct Sym {
    char name[MAX_NAME + 1];
    int kind;
    int offset;         /* S_LOCAL: frame offset */
    int depth;          /* S_LOCAL: block nesting depth (parameters: 1) */
    /* S_FUNC: */
    int returns_value;  /* 0 for void */
    int nparams;        /* -1 until known from a declaration or first call */
    int declared;       /* has a prototype or definition */
    int defined;        /* has a body */
    int code_off;       /* start of its bytecode in the code segment */
    int called;
};

extern Sym syms[MAX_SYMS];
extern int nsyms;

void parse_program(void);

/* ---------- Stage 1 -> 2: IR ops (lower.c) ----------
 * Each op's meaning, and why it differs from PLASMA's, is in ../IR.md. */

enum {
    IR_FUNC,        /* sym: start a function */
    IR_ENDFUNC,     /* a = frame size: end the current function */
    IR_CONST,       /* a = value: push a constant */
    IR_DATAADDR,    /* a = data offset: push the address of data */
    IR_LOCAL,       /* a = frame offset: push a local word */
    IR_SETLOCAL,    /* a = frame offset: store top value there, keep it */
    IR_CALL,        /* sym, a = argument count: call, push the result */
    IR_DROP,        /* discard the top value */
    IR_DUP,         /* duplicate the top value */
    IR_RETURN,      /* return the top value from the current function */
    /* binary: ( x y -- x op y ) */
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SHR,
    /* unary: ( x -- op x ) */
    IR_NEG, IR_COMP,
    IR_NOT          /* C's !: 1 if x is 0, else 0 (not PLASMA's NOT) */
};

void ir_put(int op, int a, Sym *s);

/* ---------- Stage 2 -> 3 -> 4: machine ops ---------- */

/* PLASMA opcodes used so far (see ../../doc/Bytecodes.md). */
enum {
    OP_CN = 0x00, OP_MINUS1 = 0x20, OP_LA = 0x26, OP_CB = 0x2A,
    OP_CW = 0x2C, OP_DROP = 0x30, OP_DUP = 0x34, OP_ANDI = 0x3C,
    OP_CALL = 0x54, OP_ENTER = 0x58, OP_LEAVE = 0x5A, OP_RET = 0x5C,
    OP_CFFB = 0x5E, OP_LLW = 0x66, OP_DLW = 0x6E, OP_SLW = 0x76,
    OP_NOT = 0x80, OP_ADD = 0x82, OP_SUB = 0x84, OP_MUL = 0x86, OP_DIV = 0x88,
    OP_MOD = 0x8A, OP_INCR = 0x8C, OP_DECR = 0x8E, OP_NEG = 0x90,
    OP_COMP = 0x92, OP_AND = 0x94, OP_IOR = 0x96, OP_XOR = 0x98,
    OP_SHL = 0x9A, OP_SHR = 0x9C,
    /* Pseudo-ops: markers that travel through stages 2-4 in order with
     * the code, but emit no bytes themselves. */
    OP_FUNC_BEGIN = 0x100,  /* sym: a function starts here */
    OP_FUNC_END = 0x101     /* a = frame size: the function ends here */
};

/* What an operand word refers to, if it needs relocation. */
enum { FIX_NONE, FIX_DATA, FIX_FUNC };

/* Marks an ENTER/LEAVE frame size that is filled in at function end. */
#define FRAME_LATER (-1)

typedef struct {
    int op;
    int a;          /* first operand (byte or word), or data offset */
    int b;          /* second operand (ENTER's parameter count) */
    int fix;        /* FIX_* for a word operand */
    Sym *sym;       /* FIX_FUNC target */
} Mop;

void mop_const(Mop *m, int v);         /* lower.c: make m push v */
int  mop_const_value(Mop *m, int *v);   /* lower.c: does m push a constant? */
void peep_put(Mop *m);
void peep_flush(void);

/* ---------- Stage 4: emitter (emit.c) ---------- */

/* Stream file names: the output name plus these suffixes. */
#define SUFFIX_DATA ".data"
#define SUFFIX_CODE ".code"
#define SUFFIX_SYMS ".syms"

/* Stream record kinds and symbol flags (see ../OBJECT.md). */
enum { REL_DATA = 1, REL_FUNC = 2 };
enum { SYM_DEFINED = 1, SYM_CALLED = 2, SYM_RETURNS = 4 };

void emit_open(const char *base);
void emit_put(Mop *m);
int  data_string(const char *s, int len);   /* returns offset of first char */
void emit_finish(void);

/* ---------- Final pass (link.c) ---------- */

void link_module(const char *base, const char *path);

/* Build "base" + suffix in a static buffer (main.c). */
const char *stream_name(const char *base, const char *suffix);

#endif
