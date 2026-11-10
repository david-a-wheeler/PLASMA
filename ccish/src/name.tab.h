// cccomp's table of enum constants and typedef names (name.c), as
// arrays the parser's files read directly (AGENTS.md, "Shared
// variables"): with name.h, one header, split so that only the files
// that read them pay for them in cccomp's MAX_VARS. Include after
// name.h.
#ifndef NAME_TAB_H
#define NAME_TAB_H

#define MAX_IDS      203    // limit file: enum constants and typedef names
                            // visible at once

// Entry i's value (a constant's value, or a typedef's type), its kind
// (ID_*, name.h), and its block depth plus 1 (0 once hidden). Only
// name.c changes them.
extern int id_v[MAX_IDS];
extern char id_k[MAX_IDS];
extern char id_d[MAX_IDS];
// The block depth entry i was declared at (visible entries only).
#define ID_DEPTH(i)  (id_d[i] - 1)

#endif
