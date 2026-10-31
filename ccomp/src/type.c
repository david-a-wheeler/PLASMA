/*
 * Types. A type is a handle: the index of an entry in the type table,
 * plus qualifier bits (Q_CONST, Q_VOLATILE) above it, so a qualified
 * type needs no entry of its own (approach.md, "M5 design"). Equal
 * unqualified types share one entry. The first entries are the basic
 * types, at the fixed indexes TY_VOID, TY_CHAR, TY_INT and TY_UINT
 * (unsigned int). Pointer and array types are added as they are
 * needed. A multi-dimensional array is an array of arrays: int m[3][4]
 * is an array of 3 arrays of 4 ints.
 *
 * The table is three parallel arrays, one per field, all MAX_TYPES
 * long, indexed by the same entry number (approach.md, decision 15):
 * tkind[i] is entry i's kind, tsub[i] its pointed-to or element type
 * (for a struct, its tag), and tcount[i] its element count.
 *
 * Struct tags and members are parallel arrays too (approach.md, "M6
 * design"). Tag i has name tg_name[i] (an id, or -1 if anonymous), size
 * tg_size[i] (-1 until its member list is complete), block depth
 * tg_depth[i] (-1 once hidden), and its name's hash, tg_hsum[i] and
 * tg_hmul[i], for type hashes (ccpp gives them, since cccomp has no
 * spellings). Member i
 * belongs to tag mb_tag[i], and has name mb_name[i], type mb_type[i]
 * and byte offset mb_off[i]. A struct's members are in order, but not
 * always next to each other, since a struct may be defined inside
 * another's member list.
 */
#include <stddef.h>
#include "build.h"
#include "diag.h"
#include "type.h"

#define MAX_TYPES    56     /* distinct types (46); at most 128, since
                             * char tables (fn_type[], param_types[])
                             * hold unqualified types, and a host's char
                             * may be signed */
#define MAX_TAGS     10     /* struct tags, including hidden ones (7) */
#define MAX_MEMBERS  64     /* struct members, in all structs (51) */

static char tkind[MAX_TYPES] = {TK_VOID, TK_CHAR, TK_INT, TK_UINT};
static int tsub[MAX_TYPES];     /* a handle, with qualifier bits */
static int tcount[MAX_TYPES];
static int ntypes = 4;

static int tg_name[MAX_TAGS];
static int tg_size[MAX_TAGS];
static char tg_depth[MAX_TAGS];
static unsigned tg_hsum[MAX_TAGS];
static unsigned tg_hmul[MAX_TAGS];
static int ntags;

static char mb_tag[MAX_MEMBERS];
static int mb_name[MAX_MEMBERS];
static int mb_type[MAX_MEMBERS];     /* a handle, with qualifier bits */
static int mb_off[MAX_MEMBERS];
static int nmembers;

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

/* The size of a value of type t in bytes (0 for void, an array of
 * unknown size, or an incomplete struct). */
int type_size(int t)
{
  switch (type_kind(t)) {
  case TK_CHAR:  return 1;
  case TK_INT:   return 2;
  case TK_UINT:  return 2;
  case TK_PTR:   return 2;
  case TK_ARRAY: return type_count(t) * type_size(type_sub(t));
  case TK_STRUCT:
    return tg_size[type_sub(t)] < 0 ? 0 : tg_size[type_sub(t)];
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
    fatal(20 /* "too many types" */, NULL);
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
    error(21 /* "array too large (over 32767 bytes)" */, NULL);
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

/* Type hash h with byte c added (approach.md issue 11, "Object format
 * 4"). h stays below 2^16, so a 32-bit int never overflows, and a
 * 16-bit one keeps the same low bits. */
int hash_byte(int h, int c)
{
  return (h * 31 + (c & 0xFF)) & 0xFFFF;
}

/* Type hash h with type t added, as a canonical string: k and V for
 * const and volatile, then i, u (unsigned int), c or v, * and the
 * target, [ with the
 * count and the element, or s, the tag's name and ';' (a struct is
 * known by its name in every file). */
int type_hash(int t, int h)
{
  int tag;
  if (t & Q_CONST)
    h = hash_byte(h, 'k');
  if (t & Q_VOLATILE)
    h = hash_byte(h, 'V');
  switch (type_kind(t)) {
  case TK_VOID: return hash_byte(h, 'v');
  case TK_CHAR: return hash_byte(h, 'c');
  case TK_INT:  return hash_byte(h, 'i');
  case TK_UINT: return hash_byte(h, 'u');
  case TK_PTR:  return type_hash(type_sub(t), hash_byte(h, '*'));
  case TK_ARRAY:
    h = hash_byte(hash_byte(h, '['), type_count(t));
    return type_hash(type_sub(t), hash_byte(h, type_count(t) >> 8));
  }
  /* The name's bytes, added one at a time with hash_byte(), would give
   * h * 31^n + the name's own hash from 0: ccpp gives both parts
   * (CCPP.md, TC_TAG). */
  h = hash_byte(h, 's');
  tag = struct_tag(t);
  if (tg_name[tag] >= 0)
    h = (int)(((unsigned)h * tg_hmul[tag] + tg_hsum[tag]) & 0xFFFFu);
  return hash_byte(h, ';');
}

/* Is t a struct type? */
int is_struct(int t)
{
  return type_kind(t) == TK_STRUCT;
}

/* The struct type of tag. Each tag has its own entry, so two structs
 * with the same members are different types, as in C. */
int struct_type(int tag)
{
  return find_type(TK_STRUCT, tag, 0);
}

/* The tag of struct type t. */
int struct_tag(int t)
{
  return type_sub(t);
}

/* A new, incomplete tag named name (-1 for none), visible at depth;
 * hsum and hmul are its name's hash (see type_hash()). */
int tag_new(int name, int depth, int hsum, int hmul)
{
  if (ntags >= MAX_TAGS)
    fatal(22 /* "too many structs" */, NULL);
  tg_name[ntags] = name;
  tg_hsum[ntags] = (unsigned)hsum;
  tg_hmul[ntags] = (unsigned)hmul;
  tg_size[ntags] = -1;
  tg_depth[ntags] = (char)depth;
  return ntags++;
}

/* The innermost visible tag named name: one declared at depth, or at
 * any depth if depth is negative. Returns -1 if there is none. */
int tag_find(int name, int depth)
{
  int i;
  for (i = ntags - 1; i >= 0; i--)
    if (tg_name[i] == name && tg_depth[i] >= 0
        && (depth < 0 || tg_depth[i] == depth))
      return i;
  return -1;
}

/* Hide the tags declared deeper than depth, at the end of their block.
 * They stay in the table, since types made from them may still be in
 * use (a static local's size is computed after its block ends). */
void tag_hide(int depth)
{
  int i;
  for (i = 0; i < ntags; i++)
    if (tg_depth[i] > depth)
      tg_depth[i] = -1;
}

/* The name of tag (an id), or -1 if anonymous. */
int tag_name(int tag)
{
  return tg_name[tag];
}

/* The size of tag's struct, or -1 if it is incomplete. */
int tag_size(int tag)
{
  return tg_size[tag];
}

/* Complete tag's struct, with size bytes. */
void tag_set_size(int tag, int size)
{
  tg_size[tag] = size;
}

/* Add a member to tag's struct, at byte offset off, and return it. The
 * caller checks that its name is new in the struct. */
int member_add(int tag, int name, int type, int off)
{
  if (nmembers >= MAX_MEMBERS)
    fatal(23 /* "too many struct members" */, NULL);
  mb_tag[nmembers] = (char)tag;
  mb_name[nmembers] = name;
  mb_type[nmembers] = type;
  mb_off[nmembers] = off;
  return nmembers++;
}

/* The member of tag's struct named name, or -1 if there is none. */
int member_find(int tag, int name)
{
  int i;
  for (i = 0; i < nmembers; i++)
    if (mb_tag[i] == tag && mb_name[i] == name)
      return i;
  return -1;
}

/* The member of tag's struct after member m, in order (the first if m
 * is -1), or -1 if there are no more. */
int member_next(int tag, int m)
{
  while (++m < nmembers)
    if (mb_tag[m] == tag)
      return m;
  return -1;
}

/* The type of member m, with its qualifiers. */
int member_type(int m)
{
  return mb_type[m];
}

/* The byte offset of member m in its struct. */
int member_off(int m)
{
  return mb_off[m];
}
