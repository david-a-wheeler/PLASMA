// cccomp's top level and initializers (ptop.c): what the parser's
// other files need of it (approach.md, "parse.c split design").
#ifndef PTOP_H
#define PTOP_H

// For the symbol summary (irout.c): the file's zero-filled space so far
// (its VL_BSS globals), and the modules #pragma plasma import named,
// each one's name an id. Only ptop.c changes them.
#define MAX_DEPS     4      // limit file: modules named by #pragma plasma
                            // import (1)
extern int bss_next;
extern int dep_id[MAX_DEPS];

// Set by parse_scalar_init(): whether the initializer it parsed was
// an integer constant (not an address), and its value. A const
// variable initialized so has a known value (set_known()).
extern int init_known, init_val;

// Parse the initializer of a global of type type, named name, and
// write its bytes to the data stream. Returns the type, with an array
// size that was left out filled in.
int parse_global_init(int type, int name);

// Parse the brace initializer of a local array or struct of type
// type, named name, at frame offset base, as stores into the frame.
// Returns the type, with an array size that was left out filled in.
int parse_local_init(int type, int name, int base);

// Begin a file-scope function at its parameter list (parse_declarator(),
// D_PARAMS), and return its parameter count, or -1 for "()".
int begin_function(int name);

// Declare function name of function type type, as int f(); does.
int  declare_function(int name, int type, int is_static);
int  parse_asm_label(void);         // __asm__ (NAME): NAME's id, or 0
void note_import(int key, int label);   // a function or IMP_DATA + v

// An extern declaration in a block, after the "extern".
void parse_block_extern(void);

// A _Static_assert declaration, at its keyword.
void parse_static_assert(void);

#endif
