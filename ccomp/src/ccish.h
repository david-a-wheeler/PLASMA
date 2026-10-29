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
#define MAX_SYMS     256    /* functions */
#define MAX_GLOBALS  128    /* global variables */
#define MAX_PARAMS   16
#define MAX_LOCALS   128    /* parameters and locals visible at once */
#define MAX_FUNC_CODE 4096  /* bytecode in one function */
#define MAX_FUNC_RELOCS 256 /* relocations in one function */
#define MAX_RETURNS  128    /* frame-size bytes (ENTER, LEAVEs) in one function */
#define MAX_RELOCS   2048   /* final pass: relocations in the module */
#define MAX_NEST     32     /* nesting of parentheses and blocks */
#define MAX_LABELS   256    /* branch targets in one function */
#define MAX_TYPES    128    /* distinct types */
#define MAX_PARAM_TYPES 512 /* parameter types of all declared functions */
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
    /* keywords supported now, in the order of keywords[] in lex.c */
    T_INT, T_VOID, T_RETURN, T_IF, T_ELSE, T_WHILE, T_DO, T_FOR, T_BREAK,
    T_CONTINUE, T_CHAR, T_SIZEOF, T_STATIC,
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
int  next_is_rbracket(void);        /* is the token after this one ']'? */
const char *tok_desc(void);         /* the current token, for messages */

/* ---------- Types (type.c) ----------
 * A type is an index into types[]. Equal types share one entry, so two
 * types are the same exactly when their indexes are (approach.md, "M4
 * design"). The basic types have fixed indexes. */

enum { TK_VOID, TK_CHAR, TK_INT, TK_PTR, TK_ARRAY };  /* Type.kind */
enum { TY_VOID, TY_CHAR, TY_INT };          /* indexes of the basic types */

#define MAX_OBJECT 32767    /* bytes in one array */

typedef struct {
    int kind;           /* TK_* */
    int sub;            /* TK_PTR: the type pointed to; TK_ARRAY: the
                         * element type */
    int count;          /* TK_ARRAY: the element count, or 0 if unknown
                         * (only while parsing an array parameter) */
} Type;

extern Type types[MAX_TYPES];
extern int ntypes;

int type_size(int t);
int is_pointer(int t);
int is_array(int t);
int pointer_to(int t);
int array_of(int t, int count);

/* ---------- Symbols (parse.c) ---------- */

enum { S_FUNC, S_LOCAL, S_GLOBAL };

typedef struct Sym Sym;
struct Sym {
    char name[MAX_NAME + 1];
    int kind;
    int type;           /* a variable's type; S_FUNC: the return type */
    int offset;         /* S_LOCAL: frame offset; S_GLOBAL: data offset */
    int depth;          /* S_LOCAL: block nesting depth (parameters: 1) */
    /* S_FUNC: */
    int ptypes;         /* index of the parameter types in param_types[]
                         * (parse.c), or -1 until a declaration gives them */
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
    IR_STRADDR,     /* a = string pool offset: push a string's address */
    IR_LOCAL,       /* a = frame offset: push a local word */
    IR_SETLOCAL,    /* a = frame offset: store top value there, keep it */
    IR_LOCALB,      /* a = frame offset: push a local byte (a char) */
    IR_SETLOCALB,   /* a = frame offset: store the top value's low byte
                     * there, and keep that byte as the value */
    IR_LOCALADDR,   /* a = frame offset: push the address of a local */
    IR_GLOBAL,      /* a = data offset: push a global word */
    IR_SETGLOBAL,   /* a = data offset: store top value there, keep it */
    IR_GLOBALB,     /* a = data offset: push a global byte (a char) */
    IR_SETGLOBALB,  /* a = data offset: store the top value's low byte
                     * there, and keep that byte as the value */
    IR_LOAD,        /* ( addr -- word ) load a word */
    IR_LOADB,       /* ( addr -- byte ) load a byte (a char) */
    IR_STORE,       /* ( val addr -- ) store a word */
    IR_STOREB,      /* ( val addr -- ) store val's low byte */
    IR_CALL,        /* sym, a = argument count: call, push the result */
    IR_DROP,        /* discard the top value */
    IR_DUP,         /* duplicate the top value */
    IR_RETURN,      /* return the top value from the current function */
    /* binary: ( x y -- x op y ) */
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SHR,
    /* comparisons, binary, giving C's 1 or 0 (not PLASMA's $FFFF) */
    IR_EQ, IR_NE, IR_LT, IR_GT, IR_LE, IR_GE,
    /* unary: ( x -- op x ) */
    IR_NEG, IR_COMP,
    IR_NOT,         /* C's !: 1 if x is 0, else 0 (not PLASMA's NOT) */
    /* control flow; a = label number, local to the function */
    IR_LABEL,       /* a branch target */
    IR_JUMP,        /* always branch */
    IR_BRFALSE,     /* ( x -- ) branch if x is 0 */
    IR_BRTRUE,      /* ( x -- ) branch if x is not 0 */
    IR_BRAND,       /* ( x -- x ) branch if 0, keeping it; else ( x -- ) */
    IR_BROR         /* ( x -- x ) branch if not 0, keeping it; else ( x -- ) */
};

void ir_put(int op, int a, Sym *s);
void ir_flush(void);
int ir_here(void);
int ir_holds(int pos);
void ir_rotate(int m0, int m1);

/* ---------- Stage 2 -> 3 -> 4: machine ops ---------- */

/* PLASMA opcodes used so far (see ../../doc/Bytecodes.md). */
enum {
    OP_CN = 0x00, OP_MINUS1 = 0x20, OP_BREQ = 0x22, OP_BRNE = 0x24,
    OP_LA = 0x26, OP_LLA = 0x28, OP_CB = 0x2A,
    OP_CW = 0x2C, OP_DROP = 0x30, OP_DUP = 0x34, OP_ANDI = 0x3C,
    OP_ISEQ = 0x40, OP_ISNE = 0x42, OP_ISGT = 0x44, OP_ISLT = 0x46,
    OP_ISGE = 0x48, OP_ISLE = 0x4A, OP_BRFLS = 0x4C, OP_BRTRU = 0x4E,
    OP_BRNCH = 0x50,
    OP_CALL = 0x54, OP_ENTER = 0x58, OP_LEAVE = 0x5A, OP_RET = 0x5C,
    OP_CFFB = 0x5E, OP_LB = 0x60, OP_LW = 0x62, OP_LLB = 0x64,
    OP_LLW = 0x66, OP_LAB = 0x68, OP_LAW = 0x6A, OP_DLB = 0x6C,
    OP_DLW = 0x6E, OP_SB = 0x70, OP_SW = 0x72, OP_SLB = 0x74, OP_SLW = 0x76,
    OP_SAB = 0x78, OP_SAW = 0x7A, OP_DAB = 0x7C, OP_DAW = 0x7E,
    OP_NOT = 0x80, OP_ADD = 0x82, OP_SUB = 0x84, OP_MUL = 0x86, OP_DIV = 0x88,
    OP_MOD = 0x8A, OP_INCR = 0x8C, OP_DECR = 0x8E, OP_NEG = 0x90,
    OP_COMP = 0x92, OP_AND = 0x94, OP_IOR = 0x96, OP_XOR = 0x98,
    OP_SHL = 0x9A, OP_SHR = 0x9C, OP_IDXW = 0x9E, OP_BRAND = 0xAC,
    OP_BROR = 0xAE,
    /* Pseudo-ops: markers that travel through stages 2-4 in order with
     * the code, but emit no bytes themselves. */
    OP_FUNC_BEGIN = 0x100,  /* sym: a function starts here */
    OP_FUNC_END = 0x101,    /* a = frame size: the function ends here */
    OP_LABEL = 0x102        /* a = label number: a branch target is here */
};

/* For a branch op (BRNCH, BRFLS, ...), Mop.a is the label number; stage
 * 4 turns it into a relative offset. */

/* What an operand word refers to, if it needs relocation: a data
 * offset, a function, or an offset in the string pool. */
enum { FIX_NONE, FIX_DATA, FIX_FUNC, FIX_STR };

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
#define SUFFIX_DREL ".drel"
#define SUFFIX_STRS ".strs"
#define SUFFIX_CODE ".code"
#define SUFFIX_SYMS ".syms"

/* Stream record kinds and symbol flags (see ../OBJECT.md). A REL_DATA
 * target is a data offset, a REL_STR target an offset in the string
 * pool, and a REL_FUNC target a symbol index. */
enum { REL_DATA = 1, REL_FUNC = 2, REL_STR = 3 };
enum { SYM_DEFINED = 1, SYM_CALLED = 2, SYM_RETURNS = 4 };

void emit_open(const char *base);
void emit_put(Mop *m);
int  data_string(const char *s, int len);   /* pool offset of first char */
int  data_offset(void);                     /* data bytes written so far */
void data_byte(int v);
void data_word(int v);
void data_addr(int kind, int target);       /* a relocated word */
int  data_zero(int n);                      /* returns offset of n 0 bytes */
void emit_finish(void);

/* ---------- Final pass (link.c) ---------- */

void link_module(const char *base, const char *path);

/* Build "base" + suffix in a static buffer (main.c). */
const char *stream_name(const char *base, const char *suffix);

#endif
