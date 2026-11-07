/*
 * cccomp's statements and declarations (pstmt.c): what the parser's
 * other files need of it (approach.md, "parse.c split design").
 */
#ifndef PSTM_H
#define PSTM_H

/* Parse any array sizes ("[3][4]") after a declarator's name, and
 * return the declared type, given the type t before them. If may_omit
 * is set (an array parameter), the first size may be left out. */
int  parse_dims(int t, int may_omit);

/* Parse the start of a declarator after the base type base: any '*'s,
 * then the name, whose id is stored in *name. Returns the type so far. */
int  parse_declarator(int base, int *name);

/* Parse the rest of a variable's declarator after its name (any array
 * sizes) and return its full type. If may_omit is set, the first array
 * size may be left out, for an initializer to give. */
int  parse_var_type(int type, int may_omit);

/* typedef type name, ...; in the current block. */
void parse_typedef(void);

/* Record that the variable s has the known value v, if reading it may
 * give v at compile time. */
void set_known(int s, int v);

/* Parse a parameter list starting at '('. Returns the parameter count,
 * or -1 for "()" (unspecified in a prototype, none in a definition). */
int  parse_struct(void);
int  parse_union(void);
int  parse_enum(void);

int  parse_params(void);

/* A { } block. new_scope is 0 for a function body, whose outermost
 * block shares the parameters' scope, as in C. */
void parse_block(int new_scope);

#endif
