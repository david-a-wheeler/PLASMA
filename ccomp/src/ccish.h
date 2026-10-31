/*
 * ccish - a compiler for "C-ish", a growing subset of C, that emits
 * PLASMA VM modules. See ../approach.md for the design and
 * ../../doc/Bytecodes.md for the target format.
 *
 * First the preprocessor and lexer, the program ccpp (pp*.c), turns the
 * source into a token stream, NAME.i, and a name file, NAME.nam
 * (../CCPP.md). Then the compiler is a pipeline of four stages that
 * pass a stream of ops along, with no syntax tree. Stage 1 is the
 * front end, the program cccomp; stages 2 to 4 are the back end, the
 * program ccgen; the IR goes between them in a file, NAME.ir
 * (approach.md 18.7; formats in ../OBJECT.md):
 *
 *   tokin.c  the reader of NAME.i, a token at a time
 *   parse.c  stage 1: tokens -> IR ops          (ir_put)
 *   irout.c  the IR buffer, and the writer of NAME.ir: IR ops, data,
 *            strings, and the symbol summary at the end
 *   irin.c   the reader of NAME.ir                (lower_op)
 *   lower.c  stage 2: IR ops -> machine ops     (peep_put)
 *   peep.c   stage 3: peephole optimization     (emit_put)
 *   emit.c   stage 4: encode bytes, one function at a time, and write
 *            them to the stream file, NAME.obj; then NAME.syms
 *
 * Then a separate final pass, link.c (the program ccrel), reads only
 * the stream files and summaries and writes the REL module (approach.md
 * 18.1). It uses none of the compiler's tables.
 *
 * Fixed-size tables are used on purpose: the compiler is meant to run
 * on a 64K machine eventually, so its limits should be visible.
 */
#ifndef CCISH_H
#define CCISH_H

#include <stdio.h>

/* ---------- Builds ----------
 * The full build (the default) has everything. The small build, for
 * the 64K Apple II, is made with -DCCISH_SMALL, and leaves out what a
 * valid program doesn't need (approach.md decision 21 and 18.6 C1,
 * "Two builds: the plan"). Code tests these feature macros, never
 * CCISH_SMALL itself:
 *   CCISH_LISTING  -l (ccrel's listing), and what only it uses. ccgen's
 *                  -i is in both builds (approach.md decision 22).
 *   CCISH_CHECKS   checks that only a compiler bug or a damaged file
 *                  would fail: so far, ccgen's check of each IR_SWAP's
 *                  and unsigned comparison's blocks (irin.c). Both
 *                  builds write the same files.
 */
#ifndef CCISH_SMALL
#define CCISH_LISTING
#define CCISH_CHECKS
#endif

/* ---------- Limits ----------
 * Sized just above what ccish's own source needs (approach.md, "Table
 * sizes"), since every byte counts on a 64K machine; the peak each
 * comes from is in parentheses. */

#define MAX_NAME     31     /* significant characters in an identifier */
#define MAX_STR      255    /* bytes in one string literal, so that it
                             * is a PLASMA string too (85) */
#define MAX_LINE     255    /* characters in one source line, ccpp's
                             * buffer (99) */
#define MAX_PP_POOL  7680   /* bytes of ccpp's names and file names,
                             * with a length byte each (6,890: parse.c,
                             * preprocessed by gcc) */
#define MAX_FUNCS    302    /* functions (274) */
#define MAX_VARS     82     /* globals, plus the parameters and locals
                             * visible at once (74) */
#define MAX_PARAMS   16
#define MAX_FUNC_CODE 1280  /* bytecode in one function (1,053) */
#define MAX_FUNC_RELOCS 128 /* relocations in one function (112) */
#define MAX_RETURNS  32     /* frame-size bytes (ENTER, LEAVEs) in one
                             * function (23) */
#define MAX_TEMP_USES 32    /* operands on ccgen's own temporaries in one
                             * function: 4 to 6 for each IR_SWAP done
                             * when the code runs */
#define MAX_NEST     32     /* nesting of parentheses and blocks */
#define MAX_LABELS   104    /* branch targets in one function (90); at
                             * most 128, since case_label[] is char */
#define MAX_TYPES    56     /* distinct types (46); at most 128, since
                             * char tables (fn_type[], param_types[])
                             * hold unqualified types, and a host's char
                             * may be signed */
#define MAX_PARAM_TYPES 433 /* parameter types of all declared functions
                             * (393) */
#define MAX_TAGS     10     /* struct tags, including hidden ones (7) */
#define MAX_MEMBERS  64     /* struct members, in all structs (51) */
#define MAX_FNAMES   3281   /* bytes of the function names ccgen keeps,
                             * with a NUL each (2,982: all of parse.c's
                             * function symbols, a bound) */
#define MAX_IDS      280    /* enum constants and typedef names (273, in
                             * tokin.c) */
#define MAX_BYVAL    8      /* bytes in a struct passed or returned by value */
#define MAX_CASES    36     /* case labels in all the switches open at once
                             * (29); at most 255, a SEL table's limit */
#define BLOCK        512    /* file I/O buffer size: one ProDOS block */
#define MAX_PATH     128    /* a file name's characters, with its NUL: a
                             * ProDOS path has at most 64, or 128 with a
                             * prefix (diag.c's buffers) */

/* ---------- Diagnostics and program support (diag.c) ----------
 * Shared by the programs ccish (both passes), cccomp (the compile
 * pass) and ccrel (the final pass); see approach.md, "B3". */

extern const char *src_name;
extern int src_line;
#ifdef CCISH_LISTING
extern int opt_list;        /* -l: print a listing of the generated code */
#else
#define opt_list 0          /* no listing in the small build */
#endif
extern int opt_ir;          /* -i: print the IR as ccgen reads it */
extern int opt_keep;        /* -k: keep the stream files */

/* Diagnostics take a message number: its text is in the message file,
 * ccish.msg, and is also written, in double quotes, in a comment right
 * after the number wherever it is used. A part is another message
 * number, printed after the text (0 for none). */
void error(int msg, const char *detail);
void error_in(int msg, int part, const char *detail);
void fatal(int msg, const char *detail);
void warning(int msg, const char *detail);
void warning_in(int msg, int part, const char *detail);
void msg_init(const char *argv0);           /* find ccish.msg */
const char *stream_name(const char *base, const char *suffix);
void remove_stream(const char *base, const char *suffix);  /* unless -k */
void remove_streams(const char *base);      /* .obj and .syms, unless -k */
const char *base_of(const char *src);       /* "x.c" -> "x" */
const char *num_text(int n);                /* n in decimal (no printf) */

/* Defined by each program: after an error, close the open files and
 * remove partial output. */
void clean_up(void);

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
void bskip(Bfile *b, int n);
void bputc(Bfile *b, int c);
void bputw(Bfile *b, int v);
void bwrite(Bfile *b, const unsigned char *p, int n);
int  bclose(Bfile *b);

/* ---------- Names (name.c) ----------
 * A name is its id, the number ccpp gave it (../CCPP.md): cccomp keeps
 * no spellings, and looks one up in the name file only to print it
 * (tok_name(), in tokin.c). -1 is no name. */

/* Enum constants and typedef names: the ordinary identifiers that
 * aren't variables or functions. Each has a value (a constant's value,
 * or a typedef's type handle) and the block depth it was declared at
 * (0: file scope), and is visible until that block ends. */
enum { ID_CONST, ID_TYPEDEF };
int id_add(int name, int kind, int val, int depth);
int id_find(int name);              /* the innermost visible, or -1 */
int id_kind(int i);
int id_val(int i);
int id_depth(int i);
void id_hide(int depth);            /* hide those declared deeper */

/* ---------- Types (type.c) ----------
 * A type is a handle: an int whose low 8 bits index the type table,
 * and whose bits Q_CONST and Q_VOLATILE are its qualifiers
 * (approach.md, "M5 design"). Equal unqualified types share one table
 * entry, so two types are the same exactly when their handles are.
 * The basic types have fixed indexes. A qualified type needs no entry
 * of its own: const char is TY_CHAR | Q_CONST. Only a pointer to a
 * qualified type does, since const char * and char * differ. An array
 * is never qualified itself; its elements are (const int t[3]). A
 * struct type has one entry per tag, made by struct_type().
 * The table is private to type.c; use the functions below. */

enum { TK_VOID, TK_CHAR, TK_INT, TK_PTR, TK_ARRAY, TK_STRUCT, TK_UINT };
/* the basic types' indexes: TY_UINT is unsigned int */
enum { TY_VOID, TY_CHAR, TY_INT, TY_UINT };
#define Q_CONST    0x100
#define Q_VOLATILE 0x200
#define Q_ALL      0x300
#define TY_INDEX   0xFF     /* MAX_TYPES must be at most 256 */

#define MAX_OBJECT 32767    /* bytes in one array */

int type_kind(int t);       /* TK_* */
int type_sub(int t);        /* pointer: the type pointed to; array: the
                             * element type (handles, with qualifiers) */
int type_count(int t);      /* array: the element count, or 0 if unknown
                             * (only while parsing an array parameter) */
int type_size(int t);
int is_pointer(int t);
int is_array(int t);
int pointer_to(int t);
int array_of(int t, int count);
int qualify(int t, int q);
int unqual(int t);
int is_const(int t);
int hash_byte(int h, int c);    /* a type hash with byte c added */
int type_hash(int t, int h);    /* a type hash with type t added */

/* Struct tags and members. A tag is a struct type's number (named, or
 * anonymous with name -1), and is visible from the block depth it was
 * declared at until that block ends. A tag's size is -1 until its
 * member list is complete. */
int is_struct(int t);
int struct_type(int tag);   /* the struct type of tag */
int struct_tag(int t);      /* the tag of struct type t */
int tag_new(int name, int depth, int hsum, int hmul);  /* its spelling's
                                     * hash (the token's tok_hsum and
                                     * tok_hmul) */
int tag_find(int name, int depth);  /* innermost visible tag named
                                     * name, at depth or any depth if
                                     * depth < 0; -1 if none */
void tag_hide(int depth);   /* hide the tags declared deeper */
int tag_name(int tag);
int tag_size(int tag);
void tag_set_size(int tag, int size);
int member_add(int tag, int name, int type, int off);
int member_find(int tag, int name);     /* a member, or -1 */
int member_next(int tag, int m);        /* the next member, in order */
int member_type(int m);
int member_off(int m);

/* ---------- Symbols (parse.c) ---------- */

/* Functions: declared, defined or called. A function is an index into
 * these parallel arrays (approach.md, "Sym split design"), each
 * MAX_FUNCS long. */
enum { FN_DECLARED = 1, FN_DEFINED = 2, FN_CALLED = 4, FN_COUNTED = 8,
       FN_STATIC = 16, FN_NAMED = 32 };   /* FN_NAMED: its name is in
                                           * the IR file (irout.c) */
extern int fn_name[MAX_FUNCS];      /* its name: an id */
extern char fn_type[MAX_FUNCS];     /* its return type, unqualified (C17
                                     * ignores a return type's
                                     * qualifiers), so it fits a char */
extern int fn_ptypes[MAX_FUNCS];    /* its parameter types' index in
                                     * param_types[] (parse.c), or -1
                                     * until a declaration gives them */
extern char fn_nparams[MAX_FUNCS];  /* its parameter count, once known
                                     * (FN_COUNTED) */
extern char fn_flags[MAX_FUNCS];    /* FN_* */
extern int nfuncs;

void parse_program(void);
/* For the symbol summary: function f's SYM_* flags and type hash, and
 * the number of globals and global v's flags, data offset, size, type
 * hash and name (a name pool offset). */
int  func_sym(int f, int *hash);
int  data_sym_count(void);
int  data_sym(int v, int *off, int *size, int *hash, int *name);
void compile_file(const char *in, const char *base);   /* the front end */
void compile_abort(void);                   /* after an error */

/* ---------- Stage 1 -> 2: IR ops, through NAME.ir ----------
 * Each op's meaning, and why it differs from PLASMA's, is in ../IR.md;
 * the file's format is in ../OBJECT.md. The op numbers are part of the
 * format: changing them changes IR_VERSION. */

enum {
  IR_FUNC,        /* f: start a function */
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
  IR_CALL,        /* f, a = argument count: call, push the result */
  IR_DROP,        /* discard the top value */
  IR_DUP,         /* duplicate the top value */
  IR_RETURN,      /* return the top value from the current function */
  /* binary: ( x y -- x op y ) */
  IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
  IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SHR,
  /* comparisons, binary, giving C's 1 or 0 (not PLASMA's $FFFF). LT,
   * GT and LE, GE stay adjacent pairs, IR_LT even: irin.c reverses one
   * by flipping bit 0. */
  IR_EQ, IR_NE, IR_LT, IR_GT, IR_LE, IR_GE,
  /* unary: ( x -- op x ) */
  IR_NEG, IR_COMP,
  IR_NOT,         /* C's !: 1 if x is 0, else 0 (not PLASMA's NOT) */
  /* control flow; a = label number, local to the function */
  IR_LABEL,       /* a branch target */
  IR_JUMP,        /* always branch */
  IR_BRFALSE,     /* ( x -- ) branch if x is 0 */
  IR_BRTRUE,      /* ( x -- ) branch if x is not 0 */
  IR_JOIN,        /* the branches so far to label a go to label f */
  IR_SEL,         /* ( x -- ) go to the case table at label a (a switch) */
  /* a case table, after its label: */
  IR_CASES,       /* a = the number of cases that follow */
  IR_CASEVAL,     /* a = one case's value; then: */
  IR_CASETO,      /* a = the label of that case's code */
  /* reordering (approach.md 18.7, A), done by ccgen: */
  IR_SWAP,        /* a = the top block's length in IR ops, f = the
                   * block below's, or -1 if code must not move:
                   * exchange the values the two blocks left */
  /* unsigned comparisons (18.7, C), in the order of LT GT LE GE; a =
   * the right operand's block length in IR ops, or 0 if not known */
  IR_ULT, IR_UGT, IR_ULE, IR_UGE,
  /* unsigned arithmetic PLASMA lacks, binary ( x y -- x op y ), for
   * unsigned operands; ccgen lowers them (approach.md, "unsigned and
   * helper functions design") */
  IR_UDIV, IR_UMOD, IR_USHR,
  IR_NOPS         /* the number of IR ops */
};

/* The IR file: the output name plus this suffix, and its format's
 * version (in its header, after the magic "CIR"). */
#define SUFFIX_IR   ".ir"
#define IR_VERSION  2

/* Its record tags besides the stream's D, R and S (below), and the bit
 * that marks an IR op followed by a one-byte operand. An IR op's tag is
 * its number, below IR_TAG_END. */
enum { REC_LINE = 'L', REC_NAME = 'N', REC_WORD = 'W', REC_SECOND = 'X',
       REC_SUMMARY = 'Y' };
#define IR_BYTE_OP  0x80
#define IR_TAG_END  0x40

/* A name record's length byte: the length (1 to MAX_NAME) in its low
 * bits, and NAME_RETURNS set if the function returns a value. */
#define NAME_LEN     0x1F
#define NAME_RETURNS 0x80

/* The front end's side (irout.c): the writer of NAME.ir. */
void ir_open(const char *base);
void ir_put(int op, int a, int f);   /* f: a function, or -1 */
int ir_here(void);                  /* IR ops written so far */
int  data_string(const char *s, int len);   /* pool offset of first char */
int  data_offset(void);                     /* data bytes written so far */
void data_byte(int v);
void data_word(int v);
void data_addr(int kind, int target, int add);  /* a relocated word */
int  data_zero(int n);                      /* returns offset of n 0 bytes */
void ir_finish(void);                       /* the summary, then close */
void ir_abort(void);                        /* after an error */

/* The back end's side (irin.c): read NAME.ir and pass its ops to stage
 * 2, writing NAME.obj and NAME.syms. */
void gen_file(const char *base);
void gen_abort(void);                       /* after an error */
const char *func_name(int f);       /* a function's name, for messages */
void dump_ir(int op, int a, int f);         /* lower.c: one op, for -i */

/* ---------- Stage 2 -> 3 -> 4: machine ops ---------- */

/* PLASMA opcodes used so far (see ../../doc/Bytecodes.md). */
enum {
  OP_CN = 0x00, OP_MINUS1 = 0x20, OP_BREQ = 0x22, OP_BRNE = 0x24,
  OP_LA = 0x26, OP_LLA = 0x28, OP_CB = 0x2A,
  OP_CW = 0x2C, OP_DROP = 0x30, OP_DUP = 0x34, OP_ADDI = 0x38,
  OP_SUBI = 0x3A, OP_ANDI = 0x3C,
  OP_ISEQ = 0x40, OP_ISNE = 0x42, OP_ISGT = 0x44, OP_ISLT = 0x46,
  OP_ISGE = 0x48, OP_ISLE = 0x4A, OP_BRFLS = 0x4C, OP_BRTRU = 0x4E,
  OP_BRNCH = 0x50, OP_SEL = 0x52,
  OP_CALL = 0x54, OP_ENTER = 0x58, OP_LEAVE = 0x5A, OP_RET = 0x5C,
  OP_CFFB = 0x5E, OP_LB = 0x60, OP_LW = 0x62, OP_LLB = 0x64,
  OP_LLW = 0x66, OP_LAB = 0x68, OP_LAW = 0x6A, OP_DLB = 0x6C,
  OP_DLW = 0x6E, OP_SB = 0x70, OP_SW = 0x72, OP_SLB = 0x74, OP_SLW = 0x76,
  OP_SAB = 0x78, OP_SAW = 0x7A, OP_DAB = 0x7C, OP_DAW = 0x7E,
  OP_NOT = 0x80, OP_ADD = 0x82, OP_SUB = 0x84, OP_MUL = 0x86, OP_DIV = 0x88,
  OP_MOD = 0x8A, OP_INCR = 0x8C, OP_DECR = 0x8E, OP_NEG = 0x90,
  OP_COMP = 0x92, OP_AND = 0x94, OP_IOR = 0x96, OP_XOR = 0x98,
  OP_SHL = 0x9A, OP_SHR = 0x9C, OP_IDXW = 0x9E, OP_ADDLB = 0xB0, OP_ADDLW = 0xB2, OP_ADDAB = 0xB4,
  OP_ADDAW = 0xB6, OP_IDXLB = 0xB8, OP_IDXLW = 0xBA, OP_IDXAB = 0xBC,
  OP_IDXAW = 0xBE,
  /* Pseudo-ops: markers that travel through stages 2-4 in order with
   * the code, but emit no bytes themselves. */
  OP_FUNC_BEGIN = 0x100,  /* fn: a function starts here */
  OP_FUNC_END = 0x101,    /* a = frame size: the function ends here */
  OP_LABEL = 0x102,       /* a = label number: a branch target is here */
  /* A SEL's case table (see ../../doc/Bytecodes.md), as data in the
   * code: */
  OP_CASES = 0x103,       /* a = the count byte */
  OP_CASEVAL = 0x104,     /* a = a case's value word */
  OP_CASETO = 0x105,      /* a = the label its offset word leads to */
  OP_JOIN = 0x106         /* the branches so far to label a go to
                           * label fn instead */
};

/* For a branch op (BRNCH, BRFLS, ...), Mop.a is the label number; stage
 * 4 turns it into a relative offset. */

/* What an operand word refers to, if it needs relocation: a data
 * offset, a function, or an offset in the string pool. */
enum { FIX_NONE, FIX_DATA, FIX_FUNC, FIX_STR, FIX_DSYM, FIX_HELPER };

/* Marks an ENTER/LEAVE frame size that is filled in at function end. */
#define FRAME_LATER (-1)

/* A frame operand (LLW, SLW, ...) for one of the back end's own two
 * temporaries, k = 0 or 1, which stage 4 places past the parser's
 * frame when the function ends (IR_SWAP's run-time exchange). */
#define FRAME_TEMP(k) (-2 - (k))

/* A machine op: 8 bytes in ccish (four words), so that indexing the
 * peephole window is a shift, not a multiply. */
typedef struct {
  int op;
  int a;          /* first operand (byte or word), or data offset */
  int fix;        /* FIX_* for a word operand */
  int fn;         /* FIX_FUNC target; FIX_DSYM data symbol (global
                   * variable), with a the addend; OP_FUNC_BEGIN's
                   * function; the second operand of ENTER (its
                   * parameter count) and of OP_JOIN (the label to go to) */
} Mop;

void lower_op(int op, int a, int f);    /* lower.c: stage 2's entry */
void mop_const(Mop *m, int v);         /* lower.c: make m push v */
int  mop_const_value(Mop *m, int *v);   /* lower.c: does m push a constant? */
void peep_put(Mop *m);
void peep_flush(void);

/* ---------- Stage 4: emitter (emit.c) ---------- */

/* Stream file names: the output name plus these suffixes. The back end
 * writes one stream of tagged records, then the symbol summary. */
#define SUFFIX_OBJ  ".obj"
#define SUFFIX_SYMS ".syms"
#define OBJ_VERSION 5       /* the format version, in the symbol summary */
#define DATA_RUN    128     /* bytes in one data record at most */

/* Record tags in the stream (see ../OBJECT.md), as letters so that a
 * dump of the file is easy to read. */
enum { REC_DATA = 'D', REC_DREL = 'R', REC_STR = 'S', REC_FUNC = 'F',
       REC_HELPER = 'H' };

/* Relocation kinds and symbol flags. A REL_DATA target is a data
 * offset, a REL_STR target an offset in the string pool, a REL_FUNC
 * target a function symbol index, and a REL_DSYM target a data symbol
 * index, with an addend. SYM_CALLED is "used" for a data symbol;
 * SYM_RETURNS, SYM_CMDSYS and SYM_NOTCMDSYS (a CMDSYS name declared
 * differently) are for functions. */
enum { REL_DATA = 1, REL_FUNC = 2, REL_STR = 3, REL_DSYM = 4,
       REL_HELPER = 5 };
enum { SYM_DEFINED = 1, SYM_CALLED = 2, SYM_RETURNS = 4, SYM_LOCAL = 8,
       SYM_CMDSYS = 16, SYM_NOTCMDSYS = 32 };

extern int fn_code[MAX_FUNCS];  /* each function's start in the code
                                 * segment, once written; else -1 */

/* Helper functions (approach.md, "unsigned and helper functions
 * design"): code ccgen writes into each object that uses it, as an H
 * record, of which ccrel keeps one copy. A call to helper k is a CALL
 * with relocation REL_HELPER, target k. In ccgen, helper k is function
 * number HELPER_FN(k) while it is lowered (helper.c). */
enum { HELP_UDIV, HELP_UMOD, HELP_USHR, NHELPERS };
#define HELPER_FN(k) (MAX_FUNCS + (k))
extern int helper_used;             /* bit k: helper k is called */
extern int helper_size[NHELPERS];   /* once written: its bytes */
extern int helper_sum[NHELPERS];    /* and their checksum */
void gen_helpers(void);             /* write the helpers used */
const char *helper_name(int k);

void emit_open(const char *base);
void emit_put(Mop *m);
void emit_byte(int c);              /* one byte of a record, as is */
void emit_word(int v);
int  emit_code_size(void);          /* code bytes written so far */
void emit_summary(void);            /* close .obj, start .syms */
void emit_finish(void);             /* close .syms */
void emit_abort(void);              /* after an error */

/* ---------- Final pass (link.c) ---------- */

void link_module(int n, const char **bases, const char *path);
void link_abort(void);                      /* after an error */

#endif
