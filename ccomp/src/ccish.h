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
 *   emit.c   stage 4: encode bytes, write the REL module
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
#define MAX_CODE     16384
#define MAX_DATA     8192
#define MAX_FIXUPS   1024
#define MAX_IMPORTS  128
#define MAX_NEST     32     /* nesting of parentheses and blocks */

/* ---------- Diagnostics (main.c) ---------- */

extern const char *src_name;
extern int src_line;
extern int opt_list;        /* -l: print a listing of the generated code */
extern int opt_ir;          /* -i: print the IR stream */

void error(const char *msg, const char *detail);
void fatal(const char *msg, const char *detail);

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

void lex_open(FILE *f);
void next(void);                    /* advance to the next token */
const char *tok_desc(void);         /* the current token, for messages */

/* ---------- Symbols (parse.c) ---------- */

enum { S_FUNC, S_PARAM };

typedef struct Sym Sym;
struct Sym {
    char name[MAX_NAME + 1];
    int kind;
    int offset;         /* S_PARAM: frame offset */
    /* S_FUNC: */
    int returns_value;  /* 0 for void */
    int nparams;        /* -1 until known from a declaration or first call */
    int declared;       /* has a prototype or definition */
    int defined;        /* has a body */
    int code_off;       /* start of its bytecode in the code buffer */
    int import_idx;     /* ESD index if imported, else -1 */
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
    IR_ADD,         /* replace the top two values with their sum */
    IR_CALL,        /* sym, a = argument count: call, push the result */
    IR_DROP,        /* discard the top value */
    IR_RETURN       /* return the top value from the current function */
};

void ir_put(int op, int a, Sym *s);

/* ---------- Stage 2 -> 3 -> 4: machine ops ---------- */

/* PLASMA opcodes used so far (see ../../doc/Bytecodes.md). */
enum {
    OP_CN = 0x00, OP_MINUS1 = 0x20, OP_LA = 0x26, OP_CB = 0x2A,
    OP_CW = 0x2C, OP_DROP = 0x30, OP_CALL = 0x54, OP_ENTER = 0x58,
    OP_LEAVE = 0x5A, OP_RET = 0x5C, OP_CFFB = 0x5E, OP_LLW = 0x66,
    OP_ADD = 0x82,
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

void peep_put(Mop *m);
void peep_flush(void);

/* ---------- Stage 4: emitter and module writer (emit.c) ---------- */

void emit_put(Mop *m);
int  data_string(const char *s, int len);   /* returns offset of first char */
void write_module(const char *path);

#endif
