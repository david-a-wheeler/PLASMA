/*
 * Types. A type is an index into types[], and equal types share one
 * entry (approach.md, "M4 design"). The first entries are the basic
 * types, at the fixed indexes TY_VOID, TY_CHAR and TY_INT. Pointer types
 * are added as they are needed (array types will be too).
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
    case TK_PTR:  return 2;
    }
    return 0;
}

/* Is t a pointer type? */
int is_pointer(int t)
{
    return types[t].kind == TK_PTR;
}

/* The type "pointer to t", added to the table if it isn't there yet. */
int pointer_to(int t)
{
    int i;
    for (i = 0; i < ntypes; i++)
        if (types[i].kind == TK_PTR && types[i].sub == t)
            return i;
    if (ntypes >= MAX_TYPES)
        fatal("too many types", NULL);
    types[ntypes].kind = TK_PTR;
    types[ntypes].sub = t;
    types[ntypes].count = 0;
    return ntypes++;
}
