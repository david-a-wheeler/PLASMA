/*
 * Types. A type is an index into types[], and equal types share one
 * entry (approach.md, "M4 design"). The first entries are the basic
 * types, at the fixed indexes TY_VOID, TY_CHAR and TY_INT. Pointer and
 * array types will be added as they are needed.
 */
#include "ccish.h"

Type types[MAX_TYPES] = {
    {TK_VOID, 0, 0},
    {TK_CHAR, 0, 0},
    {TK_INT, 0, 0}
};
int ntypes = 3;

/* The size of a value of type t in bytes (0 for void). */
int type_size(int t)
{
    switch (types[t].kind) {
    case TK_CHAR: return 1;
    case TK_INT:  return 2;
    }
    return 0;
}
