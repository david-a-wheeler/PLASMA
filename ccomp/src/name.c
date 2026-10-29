/*
 * The name pool: each name stored once, as a NUL-terminated string in
 * one char array, so a table holds a 2-byte offset instead of a 32-byte
 * name (approach.md, "M6 design"). Struct tags and members use it now;
 * the symbol tables can later (approach.md 18.4).
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
        fatal("too many names", "(the name pool is full)");
    memcpy(pool + pool_used, s, n);
    pool_used += n;
    return pool_used - n;
}

/* The name at offset n of the pool. */
const char *name_of(int n)
{
    return pool + n;
}
