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

#include "build.h"

/* ---------- Limits ----------
 * Sized just above what ccish's own source needs (approach.md, "Table
 * sizes"), since every byte counts on a 64K machine; the peak each
 * comes from is in parentheses. */

#include "cclimits.h"
#include "obj.h"
#include "ir.h"
#define MAX_STR      255    /* bytes in one string literal, so that it
                             * is a PLASMA string too (85) */
#define MAX_LINE     255    /* characters in one source line, ccpp's
                             * buffer (99) */
#define MAX_INCLUDE  3      /* #include files open at once, beside the
                             * source (3, from ccish.h's <stdio.h>'s
                             * <stddef.h>: one more than the Apple II
                             * can, with the source and NAME.i; the
                             * header split is to make it 2) */
#define MAX_IF_NEST  8      /* #ifdef groups open at once (3) */
#define MAX_MACROS   96     /* macros defined at once (86, in pp.c) */
#define MAX_MACRO_BYTES 512 /* their bodies, as tokens (274): room for
                             * more than ccish's own, which are short */
#define MAX_EXPAND   16     /* macro expansions inside one another (2) */
#define MAX_EXP_BYTES 256   /* bytes of the expansions and arguments
                             * being read, and of the arguments' own
                             * expansions (two buffers; 15 and 3): room
                             * for macros longer than ccish's */
#define MAX_ARG_SLOTS 32    /* arguments of the calls being expanded (1) */
#define MAX_PP_PARAMS 16    /* parameters of one macro */
#define MAX_INC_DIRS 8      /* -I directories */
#define MAX_PP_OPTS  16     /* -D and -U options */
#define MAX_PP_POOL  9000   /* bytes of ccpp's names and file names,
                             * with a length byte each (8,179: parse.c
                             * with its headers, macros' names too) */
#define MAX_PARAMS   16
#define MAX_RETURNS  32     /* frame-size bytes (ENTER, LEAVEs) in one
                             * function (23) */
#define MAX_TEMP_USES 32    /* operands on ccgen's own temporaries in one
                             * function: 4 to 6 for each IR_SWAP done
                             * when the code runs */
#define MAX_NEST     32     /* nesting of parentheses and blocks */
#define MAX_TYPES    56     /* distinct types (46); at most 128, since
                             * char tables (fn_type[], param_types[])
                             * hold unqualified types, and a host's char
                             * may be signed */
#define MAX_PARAM_TYPES 433 /* parameter types of all declared functions
                             * (393) */
#define MAX_TAGS     10     /* struct tags, including hidden ones (7) */
#define MAX_MEMBERS  64     /* struct members, in all structs (51) */
#define MAX_IDS      280    /* enum constants and typedef names (273, in
                             * tokin.c) */
#define MAX_BYVAL    8      /* bytes in a struct passed or returned by value */
#define MAX_CASES    36     /* case labels in all the switches open at once
                             * (29); at most 255, a SEL table's limit */
#define BLOCK        512    /* file I/O buffer size: one ProDOS block */

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

#include "mop.h"

void lower_op(int op, int a, int f);    /* lower.c: stage 2's entry */
void mop_const(Mop *m, int v);         /* lower.c: make m push v */
int  mop_const_value(Mop *m, int *v);   /* lower.c: does m push a constant? */
void peep_put(Mop *m);
void peep_flush(void);

/* ---------- Stage 4: emitter (emit.c) ---------- */

#define DATA_RUN    128     /* bytes in one data record at most */


extern int fn_code[MAX_FUNCS];  /* each function's start in the code
                                 * segment, once written; else -1 */

/* Helper functions (approach.md, "unsigned and helper functions
 * design"): code ccgen writes into each object that uses it, as an H
 * record, of which ccrel keeps one copy. A call to helper k is a CALL
 * with relocation REL_HELPER, target k. In ccgen, helper k is function
 * number HELPER_FN(k) while it is lowered (helper.c). */
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
