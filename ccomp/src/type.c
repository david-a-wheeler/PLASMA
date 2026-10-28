/*
 * Types. A type is an index into types[], and equal types share one
 * entry (approach.md, "M4 design"). The first entries are the basic
 * types, at the fixed indexes TY_VOID, TY_CHAR and TY_INT. Pointer and
 * array types are added as they are needed. A multi-dimensional array
 * is an array of arrays: int m[3][4] is an array of 3 arrays of 4 ints.
 */
#include "ccish.h"

Type types[MAX_TYPES] = {
    {TK_VOID, 0, 0},
    {TK_CHAR, 0, 0},
    {TK_INT, 0, 0}
};
int ntypes = 3;

/* The size of a value of type t in bytes (0 for void, or for an array
 * of unknown size). */
int type_size(int t)
{
    switch (types[t].kind) {
    case TK_CHAR:  return 1;
    case TK_INT:   return 2;
    case TK_PTR:   return 2;
    case TK_ARRAY: return types[t].count * type_size(types[t].sub);
    }
    return 0;
}

/* Is t a pointer type? */
int is_pointer(int t)
{
    return types[t].kind == TK_PTR;
}

/* Is t an array type? */
int is_array(int t)
{
    return types[t].kind == TK_ARRAY;
}

/* The type of kind k with sub-type sub and count count, added to the
 * table if it isn't there yet. */
static int find_type(int k, int sub, int count)
{
    int i;
    for (i = 0; i < ntypes; i++)
        if (types[i].kind == k && types[i].sub == sub
            && types[i].count == count)
            return i;
    if (ntypes >= MAX_TYPES)
        fatal("too many types", NULL);
    types[ntypes].kind = k;
    types[ntypes].sub = sub;
    types[ntypes].count = count;
    return ntypes++;
}

/* The type "pointer to t". */
int pointer_to(int t)
{
    return find_type(TK_PTR, t, 0);
}

/* The type "array of count t's" (count 0: size unknown). Its size must
 * fit in MAX_OBJECT bytes, so that sizes and offsets fit in an int on
 * the target. The caller checks that t has a size. */
int array_of(int t, int count)
{
    if (count > MAX_OBJECT / type_size(t))
        error("array too large (over 32767 bytes)", NULL);
    return find_type(TK_ARRAY, t, count);
}
