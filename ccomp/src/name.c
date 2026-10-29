/*
 * The name pool: each name stored once, as a NUL-terminated string in
 * one char array, so a table holds a 2-byte offset instead of a 32-byte
 * name (approach.md, "M6 design"). Struct tags and members use it, and
 * so does the table of enum constants and typedef names below; the
 * symbol tables can later (approach.md 18.4).
 *
 * That table is parallel arrays (approach.md, "enum and typedef
 * design"): entry i has name id_nm[i] (a pool offset), value id_v[i],
 * kind id_k[i] (ID_*), and block depth id_d[i] (-1 once hidden).
 */
#include <string.h>
#include "ccish.h"

static char pool[MAX_POOL];
static int pool_used;

/* The offset of name s in the pool, or -1 if it isn't there. */
int name_find(const char *s)
{
    int i = 0;
    while (i < pool_used) {
        if (!strcmp(pool + i, s))
            return i;
        i += (int)strlen(pool + i) + 1;
    }
    return -1;
}

/* The offset of name s in the pool, adding it if it isn't there. */
int intern(const char *s)
{
    int i = name_find(s), n = (int)strlen(s) + 1;
    if (i >= 0)
        return i;
    if (pool_used + n > MAX_POOL)
        fatal(18 /* "too many names" */, "(the name pool is full)");
    memcpy(pool + pool_used, s, n);
    pool_used += n;
    return pool_used - n;
}

/* Where the pool ends now: a mark, taken before a function's
 * parameters and locals add their names (approach.md, "Sym split
 * design"). */
int pool_mark(void)
{
    return pool_used;
}

/* At the end of a function, release the names added since mark: its
 * parameters', locals' and block-scope names, which nothing uses any
 * more (their tags and enum constants are hidden). The names of
 * functions first seen inside it (called before being declared) must
 * stay, so they are moved down to mark, in order. */
void pool_release(int mark)
{
    int f, n;
    for (f = 0; f < nfuncs; f++)
        if (fn_name[f] >= mark) {
            n = (int)strlen(pool + fn_name[f]) + 1;
            memmove(pool + mark, pool + fn_name[f], n);
            fn_name[f] = mark;
            mark += n;
        }
    pool_used = mark;
}

/* The name at offset n of the pool. */
const char *name_of(int n)
{
    return pool + n;
}

static int id_nm[MAX_IDS];
static int id_v[MAX_IDS];
static char id_k[MAX_IDS];
static char id_d[MAX_IDS];
static int nids;

/* Add an enum constant or typedef name (kind ID_*) with value val,
 * visible from block depth depth on, and return it. The caller checks
 * that the name is new in its scope. */
int id_add(int name, int kind, int val, int depth)
{
    if (nids >= MAX_IDS)
        fatal(19 /* "too many enum constants and typedef names" */, NULL);
    id_nm[nids] = name;
    id_v[nids] = val;
    id_k[nids] = (char)kind;
    id_d[nids] = (char)depth;
    return nids++;
}

/* The innermost visible enum constant or typedef named name, or -1. */
int id_find(int name)
{
    int i;
    for (i = nids - 1; i >= 0; i--)
        if (id_nm[i] == name && id_d[i] >= 0)
            return i;
    return -1;
}

/* The kind of entry i (ID_*). */
int id_kind(int i)
{
    return id_k[i];
}

/* The value of entry i: a constant's value, or a typedef's type. */
int id_val(int i)
{
    return id_v[i];
}

/* The block depth entry i was declared at. */
int id_depth(int i)
{
    return id_d[i];
}

/* Hide the entries declared deeper than depth, at the end of their
 * block, and free them if they are the last ones: unlike struct tags,
 * no type refers to them. */
void id_hide(int depth)
{
    int i;
    for (i = 0; i < nids; i++)
        if (id_d[i] > depth)
            id_d[i] = -1;
    while (nids > 0 && id_d[nids - 1] < 0)
        nids--;
}
