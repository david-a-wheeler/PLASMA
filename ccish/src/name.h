// cccomp's table of enum constants and typedef names (name.c). A name
// is its id, the number ccpp gave it (../CCPP.md): cccomp keeps no
// spellings, and looks one up in the name file only to print it
// (nam_name(), in nam.c). -1 is no name.
#ifndef NAME_H
#define NAME_H

// ENUM VALUES: assigned here; a set test or value manipulation on
// one is a #define with a _Static_assert (next to the values, or
// file-local in its only user), and the values may be reassigned
// freely as long as those checks hold. See ../doc/enums.md.

// (With name.tab.h, one header: the table itself, for the files that
// read it directly.)
//
// Enum constants, constexpr names and typedef names: the ordinary
// identifiers that aren't variables or functions. Each has a value (a
// constant's value, or a typedef's type handle) and the block depth it
// was declared at (0: file scope), and is visible until that block
// ends. A constant's kind is its type: TY_CHAR, TY_INT (ID_CONST, an
// enum constant's) or TY_UINT, so reading it gives its type for free;
// a typedef's kind is TY_VOID's number, which no constant has
// (pexpr.c asserts both).
enum { ID_TYPEDEF = 0, ID_CONST = 2 };
int id_add(int name, int kind, int val, int depth);
int id_find(int name);              // the innermost visible, or -1
void id_hide(int depth);            // hide those declared deeper
#ifdef CCISH_STATS
int id_peak(void);
#endif

#endif
