// cccomp's struct tags and members (type.c), as arrays the parser's
// files read and write directly (AGENTS.md, "Shared variables"): with
// type.h, one header, split so that only the files that use these pay
// for them in cccomp's MAX_VARS. Include after type.h.
#ifndef TYPE_TAG_H
#define TYPE_TAG_H

#define MAX_TAGS     6      // struct tags, including hidden ones (3)
#define MAX_MEMBERS  32     // struct members, in all structs (25)

// Tag i's name (an id, or -1 if anonymous), its size (-1 until its
// member list is complete: tag_new() sets that, pstmt.c the size), and
// whether it's a union's (set by pstmt.c, when it's made).
extern int tg_name[MAX_TAGS];
extern int tg_size[MAX_TAGS];
extern char tg_union[MAX_TAGS];
// Member i's type, with its qualifiers, and its byte offset in its
// struct (member_add() sets them).
extern int mb_type[MAX_MEMBERS];
extern int mb_off[MAX_MEMBERS];

#endif
