/*
 * cccomp's parser, stage 1 (parse.c): the table of functions, which
 * irout.c reads too, and the front end's entry. Include after obj.h.
 */
#ifndef PARSE_H
#define PARSE_H

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
 * hash and name (an id). */
int  func_sym(int f, int *hash);
int  data_sym_count(void);
int  bss_size(void);                /* the zero-filled space's bytes */
int  data_sym(int v, int *off, int *size, int *hash, int *name);
void compile_file(const char *in, const char *base);   /* the front end */
void compile_abort(void);                   /* after an error */

#endif
