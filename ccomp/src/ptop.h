/*
 * cccomp's top level and initializers (ptop.c): what the parser's
 * other files need of it (approach.md, "parse.c split design").
 */
#ifndef PTOP_H
#define PTOP_H

/* Set by parse_scalar_init(): whether the initializer it parsed was
 * an integer constant (not an address), and its value. A const
 * variable initialized so has a known value (set_known()). */
extern int init_known, init_val;

/* Parse the initializer of a global of type type, named name, and
 * write its bytes to the data stream. Returns the type, with an array
 * size that was left out filled in. */
int parse_global_init(int type, int name);

/* Parse the brace initializer of a local array or struct of type
 * type, named name, at frame offset base, as stores into the frame.
 * Returns the type, with an array size that was left out filled in. */
int parse_local_init(int type, int name, int base);

#endif
