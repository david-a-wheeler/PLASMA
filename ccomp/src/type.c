/*
 * Types. A type is a handle: the index of an entry in the type table,
 * plus qualifier bits (Q_CONST, Q_VOLATILE) above it, so a qualified
 * type needs no entry of its own (approach.md, "M5 design"). Equal
 * unqualified types share one entry. The first entries are the basic
 * types, at the fixed indexes TY_VOID, TY_CHAR and TY_INT. Pointer and
 * array types are added as they are needed. A multi-dimensional array
 * is an array of arrays: int m[3][4] is an array of 3 arrays of 4 ints.
 *
 * The table is three parallel arrays, one per field, all MAX_TYPES
 * long, indexed by the same entry number (approach.md, decision 15):
 * tkind[i] is entry i's kind, tsub[i] its pointed-to or element type,
 * and tcount[i] its element count.
 */
#include "ccish.h"

static char tkind[MAX_TYPES] = {TK_VOID, TK_CHAR, TK_INT};
static int tsub[MAX_TYPES];     /* a handle, with qualifier bits */
static int tcount[MAX_TYPES];
static int ntypes = 3;

/* The kind of type t (TK_*). */
int type_kind(int t)
{
    return tkind[t & TY_INDEX];
}

/* The type a pointer type t points to, or an array type t's element
 * type, with its qualifiers. */
int type_sub(int t)
{
    return tsub[t & TY_INDEX];
}

/* The element count of array type t (0 if unknown). */
int type_count(int t)
{
    return tcount[t & TY_INDEX];
}

/* The size of a value of type t in bytes (0 for void, or for an array
 * of unknown size). */
int type_size(int t)
{
    switch (type_kind(t)) {
    case TK_CHAR:  return 1;
    case TK_INT:   return 2;
    case TK_PTR:   return 2;
    case TK_ARRAY: return type_count(t) * type_size(type_sub(t));
    }
    return 0;
}

/* Is t a pointer type? */
int is_pointer(int t)
{
    return type_kind(t) == TK_PTR;
}

/* Is t an array type? */
int is_array(int t)
{
    return type_kind(t) == TK_ARRAY;
}

/* The type of kind k with sub-type sub and count count, added to the
 * table if it isn't there yet. The result is unqualified. */
static int find_type(int k, int sub, int count)
{
    int i;
    for (i = 0; i < ntypes; i++)
        if (tkind[i] == k && tsub[i] == sub && tcount[i] == count)
            return i;
    if (ntypes >= MAX_TYPES)
        fatal("too many types", NULL);
    tkind[ntypes] = (char)k;
    tsub[ntypes] = sub;
    tcount[ntypes] = count;
    return ntypes++;
}

/* The type "pointer to t" (t may be qualified: const char *). */
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

/* Type t with qualifiers q (Q_CONST, Q_VOLATILE) added. An array's
 * qualifiers go to its elements, as in C. */
int qualify(int t, int q)
{
    if (is_array(t))
        return array_of(qualify(type_sub(t), q), type_count(t));
    return t | q;
}

/* Type t without its own qualifiers (those of what it points to stay:
 * unqual of const char * is still const char *). */
int unqual(int t)
{
    return t & ~Q_ALL;
}

/* Is type t const? */
int is_const(int t)
{
    return (t & Q_CONST) != 0;
}
