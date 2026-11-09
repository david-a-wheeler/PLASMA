// cccomp's types (type.c).
#ifndef TYPE_H
#define TYPE_H

// A type is a handle: an int whose low 8 bits index the type table,
// and whose bits Q_CONST and Q_VOLATILE are its qualifiers
// (approach.md, "M5 design"). Equal unqualified types share one table
// entry, so two types are the same exactly when their handles are.
// The basic types have fixed indexes. A qualified type needs no entry
// of its own: const char is TY_CHAR | Q_CONST. Only a pointer to a
// qualified type does, since const char * and char * differ. An array
// is never qualified itself; its elements are (const int t[3]). A
// struct type has one entry per tag, made by struct_type(). A function
// type is only its return type: a call needs that to know what it
// returns, and its arguments' own types say what they push, so ccish
// keeps no parameter list (approach.md, "Function pointer design").
// The table is private to type.c; use the functions and macros below.
// (The struct tags' and members' arrays are in type.tag.h, for the
// files that use them.)

// The basic types' indexes, the table's first entries: TY_UINT is
// unsigned int. Each is its own kind, too (type.c's TK_* follow them).
enum { TY_VOID, TY_CHAR, TY_INT, TY_UINT };
// Is the unqualified type t char, int or unsigned? (Every other type's
// index is higher.)
#define IS_INT_TYPE(t)  ((t) >= TY_CHAR && (t) <= TY_UINT)
_Static_assert(TY_VOID == 0 && TY_CHAR == 1 && TY_UINT - TY_CHAR == 2,
               "the basic types aren't void, then char, int and unsigned"
               " (IS_INT_TYPE), from 0");
// A handle's qualifier bits, above its index bits: a handle is only
// those, so taking the qualifiers off is masking the index, one AND.
#define Q_CONST    0x100
#define Q_VOLATILE 0x200   // IR_VOLATILE << 4 (ir.h): parse.c's
                           // acc_form() relies on it
#define Q_ALL      0x300
#define TY_INDEX   0xFF     // MAX_TYPES must be at most 256
#define IS_CONST(t)  ((t) & Q_CONST)      // is type t const?
// Type t without its own qualifiers (those of what it points to stay:
// UNQUAL of const char * is still const char *).
#define UNQUAL(t)    ((t) & TY_INDEX)
_Static_assert(Q_ALL == (Q_CONST | Q_VOLATILE) && (Q_ALL & TY_INDEX) == 0
               && (Q_CONST & Q_VOLATILE) == 0 && TY_INDEX == 0xFF
               && (Q_ALL >> 8) == 3,
               "a handle isn't an index in its low byte with the qualifier"
               " bits above it, so UNQUAL isn't masking the index");

#define MAX_OBJECT 32767    // bytes in one array

int type_sub(int t);        // pointer: the type pointed to; array: the
                            // element type (handles, with qualifiers);
                            // function: the return type
int type_count(int t);      // array: the element count, or 0 if unknown
                            // (only while parsing an array parameter)
int type_size(int t);
int is_pointer(int t);
int is_array(int t);
int pointer_to(int t);
int array_of(int t, int count);
int func_of(int t);         // a function returning t
int is_func(int t);
int qualify(int t, int q);
int hash_byte(int h, int c);    // a type hash with byte c added
int type_hash(int t, int h);    // a type hash with type t added

// Struct tags and members. A tag is a struct type's number (named, or
// anonymous with name -1), and is visible from the block depth it was
// declared at until that block ends. A tag's size is -1 until its
// member list is complete.
int is_struct(int t);
int struct_type(int tag);   // the struct type of tag
#define STRUCT_TAG(t)  type_sub(t)  // the tag of struct type t
// A new tag; hsum and hmul are its spelling's hash (the token's
// tok_hsum and tok_hmul).
int tag_new(int name, int depth, int hsum, int hmul);
int tag_find(int name, int depth);  // innermost visible tag named
                                    // name, at depth or any depth if
                                    // depth < 0; -1 if none
void tag_hide(int depth);   // hide the tags declared deeper
int member_add(int tag, int name, int type, int off);
int member_find(int tag, int name);     // a member, or -1
int member_next(int tag, int m);        // the next member, in order
#ifdef CCISH_STATS
void type_stats(int *types, int *tags, int *members);
#endif

#endif
