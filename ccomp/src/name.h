/*
 * cccomp's table of enum constants and typedef names (name.c). A name
 * is its id, the number ccpp gave it (../CCPP.md): cccomp keeps no
 * spellings, and looks one up in the name file only to print it
 * (nam_name(), in nam.c). -1 is no name.
 */
#ifndef NAME_H
#define NAME_H

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

#endif
