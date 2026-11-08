/*
 * cccomp's statements and declarations (pstmt.c): what the parser's
 * other files need of it (approach.md, "parse.c split design").
 */
#ifndef PSTM_H
#define PSTM_H

/* parse_declarator()'s flags: the name may be left out (a parameter);
 * there is no name (a type name); a file-scope function's own
 * parameter list is parsed by begin_function() (ptop.c). */
enum { D_ABSTRACT = 1, D_NONAME = 2, D_PARAMS = 4 };

/* Parse a declarator after the base type base, in C's full syntax
 * (int (*p)(int), int *(*a[3])[4]), store its name's id in *name (-1
 * if none), and return the declared type. Only a D_PARAMS function's
 * own parameter list is parsed; any other is skipped. */
int  parse_declarator(int base, int flags, int *name);

/* After a D_PARAMS parse_declarator(): the parameter count of the
 * declared function's own list, or -2 if it had none. */
extern int decl_nparams;

/* A type name, as in a cast or sizeof: int (*)(int). */
int  parse_type_name(void);

/* Check a variable's declared type and return it. If may_omit is
 * clear, an array's size can't be left out. */
int  parse_var_type(int type, int may_omit);

/* typedef type name, ...; in the current block. */
void parse_typedef(void);

/* Record that the variable s has the known value v, if reading it may
 * give v at compile time. */
void set_known(int s, int v);

/* Parse a parameter list starting at '('. Returns the parameter count,
 * or -1 for "()" (unspecified in a prototype, none in a definition). */
int  parse_struct(int is_union);    /* struct, or union (1) */
int  parse_enum(void);

int  parse_params(void);

/* A { } block. new_scope is 0 for a function body, whose outermost
 * block shares the parameters' scope, as in C. */
void parse_block(int new_scope);

#endif
