/*
 * cccomp's types (type.c).
 */
#ifndef TYPE_H
#define TYPE_H

/*
 * A type is a handle: an int whose low 8 bits index the type table,
 * and whose bits Q_CONST and Q_VOLATILE are its qualifiers
 * (approach.md, "M5 design"). Equal unqualified types share one table
 * entry, so two types are the same exactly when their handles are.
 * The basic types have fixed indexes. A qualified type needs no entry
 * of its own: const char is TY_CHAR | Q_CONST. Only a pointer to a
 * qualified type does, since const char * and char * differ. An array
 * is never qualified itself; its elements are (const int t[3]). A
 * struct type has one entry per tag, made by struct_type(). A function
 * type is only its return type: a call needs that to know what it
 * returns, and its arguments' own types say what they push, so ccish
 * keeps no parameter list (approach.md, "Function pointer design").
 * The table is private to type.c; use the functions below. */

enum { TK_VOID, TK_CHAR, TK_INT, TK_PTR, TK_ARRAY, TK_STRUCT, TK_UINT,
       TK_FUNC };
/* the basic types' indexes: TY_UINT is unsigned int */
enum { TY_VOID, TY_CHAR, TY_INT, TY_UINT };
#define Q_CONST    0x100
#define Q_VOLATILE 0x200   /* IR_VOLATILE << 4 (ir.h): parse.c's
                             * acc_form() relies on it */
#define Q_ALL      0x300
#define TY_INDEX   0xFF     /* MAX_TYPES must be at most 256 */

#define MAX_OBJECT 32767    /* bytes in one array */

int type_kind(int t);       /* TK_* */
int type_sub(int t);        /* pointer: the type pointed to; array: the
                             * element type (handles, with qualifiers);
                             * function: the return type */
int type_count(int t);      /* array: the element count, or 0 if unknown
                             * (only while parsing an array parameter) */
int type_size(int t);
int is_pointer(int t);
int is_array(int t);
int pointer_to(int t);
int array_of(int t, int count);
int func_of(int t);         /* a function returning t */
int is_func(int t);
int qualify(int t, int q);
int unqual(int t);
int is_const(int t);
int hash_byte(int h, int c);    /* a type hash with byte c added */
int type_hash(int t, int h);    /* a type hash with type t added */

/* Struct tags and members. A tag is a struct type's number (named, or
 * anonymous with name -1), and is visible from the block depth it was
 * declared at until that block ends. A tag's size is -1 until its
 * member list is complete. */
int is_struct(int t);
int struct_type(int tag);   /* the struct type of tag */
int struct_tag(int t);      /* the tag of struct type t */
int tag_new(int name, int depth, int hsum, int hmul);  /* its spelling's
                                     * hash (the token's tok_hsum and
                                     * tok_hmul) */
int tag_find(int name, int depth);  /* innermost visible tag named
                                     * name, at depth or any depth if
                                     * depth < 0; -1 if none */
void tag_hide(int depth);   /* hide the tags declared deeper */
#ifdef CCISH_DIAG
int tag_name(int tag);           /* for messages */
#endif
int tag_size(int tag);
void tag_set_size(int tag, int size);
extern char tg_union[];         /* 1 if the tag is a union's (set by
                                 * pstmt.c, when it's made) */
int tag_is_union(int tag);
int member_add(int tag, int name, int type, int off);
int member_find(int tag, int name);     /* a member, or -1 */
int member_next(int tag, int m);        /* the next member, in order */
int member_type(int m);
int member_off(int m);
#ifdef CCISH_STATS
void type_stats(int *types, int *tags, int *members);
#endif

#endif
