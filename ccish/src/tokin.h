// cccomp's token reader (tokin.c): the tokens of NAME.i, which ccpp
// wrote (../CCPP.md), one at a time, as the parser's T_* tokens.
// Include after cclimits.h.
#ifndef TOKIN_H
#define TOKIN_H

// ENUM VALUES: the values below are assigned for the set tests and
// value manipulations on them (the #defines here, and file-local
// ones in the .c files, each with a _Static_assert of what it
// relies on), and may be reassigned freely as long as those checks
// hold; never test or transform one with a raw number outside such
// a define and assert pair. See ../doc/enums.md.

// Single-character tokens are their character code; others start at
// 256. The keywords and the operators are in the order of the token
// stream's codes (token.h; tokin.c maps one to the other by offset, and
// checks each), in blocks, each a set the parser tests: a power of 2 in
// size, starting at a multiple of its size, so that ccgen tests it with
// one AND and one compare (IN_BLOCK, build.h), as the checks below
// require. A block's spare numbers are marked UNUSED: room for a new
// token of its kind (they're not names, which would cost cccomp MAX_IDS
// entries). The stream's codes for them are spare too.
enum {
  T_EOF = 0,
  T_ID = 256, T_NUM, T_STR,
  // the keywords, a block of 64 (the stream's $80-$BF):
  // $00-$0F, the type-name keywords (T_IS_TYPE_START): the int-type
  // specifiers (T_IS_INT_TYPE), then the qualifiers (T_IS_QUAL), the
  // two a block of 8 (T_IS_INT_OR_QUAL); the tagged types
  // (T_IS_TAGGED), struct and union first (T_IS_STRUCT_UNION); void
  T_INT = 0x110, T_CHAR, T_SIGNED, T_UNSIGNED,
  T_CONST, T_VOLATILE,          // +$06-$07: UNUSED (T_IS_QUAL's 4)
  T_STRUCT = T_INT + 0x08, T_UNION, T_ENUM, // +$0B: UNUSED (T_IS_TAGGED's 4)
  T_VOID = T_INT + 0x0C,        // +$0D-$0F: UNUSED (T_IS_TYPE_START's 16)
  // $10-$17: the storage classes (T_IS_STORAGE), typedef and constexpr
  // (T_IS_ID_DECL), case and default (T_IS_CASE_LABEL)
  T_STATIC = T_INT + 0x10, T_EXTERN, T_REGISTER,
                                // +$13: UNUSED (T_IS_STORAGE's 4)
  T_TYPEDEF = T_INT + 0x14, T_CONSTEXPR,
  T_CASE, T_DEFAULT,
  // $18-$27: the other keywords
  T_RETURN, T_IF, T_ELSE, T_WHILE, T_DO, T_FOR,
  T_BREAK, T_CONTINUE,
  T_SIZEOF, T_SWITCH, T_GOTO, T_STATIC_ASSERT,
                                // +$24-$27: UNUSED (NKEYWORDS, token.h)
  // any other C keyword: reported as "not supported yet"
  T_KEYWORD = T_INT + 0x28,
  // the multi-character operators, a block of 32 (the stream's
  // $C0-$DF; not all are supported by the parser yet):
  // $00-$0F, the compound assignments (T_IS_ASSIGN_OP), in the order of
  // the binary IR ops they do (pexpr.c's T_ASSIGN_IR() relies on it)
  T_ADDEQ = 0x140, T_SUBEQ, T_MULEQ, T_ANDEQ, T_OREQ, T_XOREQ,
                                // +$06-$07: UNUSED, as IR's $36-$37
  T_DIVEQ = T_ADDEQ + 0x08, T_MODEQ, T_SHLEQ, T_SHREQ,
                                // +$0C-$0F: UNUSED (T_IS_ASSIGN_OP's 16)
  // $10-$1F: ++ and -- (T_IS_INCDEC), in IR_ADD and IR_SUB's order
  // (pexpr.c's T_INCDEC_IR()), && and || (T_IS_LOGICAL), then the rest
  T_INC = T_ADDEQ + 0x10, T_DEC, T_ANDAND, T_OROR,
  T_EQ, T_NE, T_LE, T_GE, T_SHL, T_SHR, T_ARROW, T_ELLIPSIS
                                // +$1C-$1F: UNUSED (NOPS, token.h)
};

// The sets of tokens the parser tests, each a block in the order above
// (or two, for T_IS_TYPE_START's).
#define T_IS_TYPE_START(t)  IN_BLOCK(t, T_INT, 16)
#define T_IS_INT_OR_QUAL(t) IN_BLOCK(t, T_INT, 8)
#define T_IS_INT_TYPE(t)    IN_BLOCK(t, T_INT, 4)
#define T_IS_QUAL(t)        IN_BLOCK(t, T_CONST, 4)
#define T_IS_TAGGED(t)      IN_BLOCK(t, T_STRUCT, 4)
#define T_IS_STRUCT_UNION(t) IN_BLOCK(t, T_STRUCT, 2)  // with members
#define T_IS_STORAGE(t)     IN_BLOCK(t, T_STATIC, 4)
// typedef or constexpr: names in the enum and typedef table (name.h).
#define T_IS_ID_DECL(t)     IN_BLOCK(t, T_TYPEDEF, 2)
#define T_IS_CASE_LABEL(t)  IN_BLOCK(t, T_CASE, 2)
// The compound assignments, +=, -=, ... >>=.
#define T_IS_ASSIGN_OP(t)   IN_BLOCK(t, T_ADDEQ, 16)
#define T_IS_INCDEC(t)      IN_BLOCK(t, T_INC, 2)       // ++ --
#define T_IS_LOGICAL(t)     IN_BLOCK(t, T_ANDAND, 2)    // && ||

// Each set's block is aligned, and holds its members, and no other.
_Static_assert((T_INT & 15) == 0 && T_IS_INT_TYPE(T_UNSIGNED)
               && T_IS_INT_TYPE(T_CHAR) && T_IS_INT_TYPE(T_SIGNED)
               && (T_CONST & 3) == 0 && T_IS_QUAL(T_VOLATILE)
               && T_IS_INT_OR_QUAL(T_VOLATILE) && !T_IS_INT_TYPE(T_CONST)
               && (T_STRUCT & 3) == 0 && T_IS_STRUCT_UNION(T_UNION)
               && T_IS_TAGGED(T_ENUM) && !T_IS_STRUCT_UNION(T_ENUM)
               && !T_IS_INT_OR_QUAL(T_STRUCT) && T_IS_TYPE_START(T_VOID)
               && !T_IS_TAGGED(T_VOID) && !T_IS_TYPE_START(T_STATIC),
               "the type-name keywords' blocks: the int types (4) and the"
               " qualifiers (4) a block of 8, then the tagged types (4,"
               " struct and union a pair), and void, in a block of 16");
_Static_assert((T_STATIC & 3) == 0 && T_IS_STORAGE(T_EXTERN)
               && T_IS_STORAGE(T_REGISTER)
               && (T_TYPEDEF & 1) == 0 && T_IS_ID_DECL(T_CONSTEXPR)
               && (T_CASE & 1) == 0 && T_IS_CASE_LABEL(T_DEFAULT)
               && !T_IS_STORAGE(T_TYPEDEF),
               "T_IS_STORAGE (4), T_IS_ID_DECL and T_IS_CASE_LABEL (pairs)"
               " aren't aligned blocks");
_Static_assert((T_ADDEQ & 15) == 0 && T_IS_ASSIGN_OP(T_SHREQ)
               && !T_IS_ASSIGN_OP(T_INC) && (T_INC & 1) == 0
               && T_IS_INCDEC(T_DEC) && (T_ANDAND & 1) == 0
               && T_IS_LOGICAL(T_OROR) && !T_IS_LOGICAL(T_EQ),
               "T_IS_ASSIGN_OP (16), T_IS_INCDEC and T_IS_LOGICAL (pairs)"
               " aren't aligned blocks");
// A one-character token is its character code ($20-$7E), so no other
// token may be one.
_Static_assert(T_EOF < ' ' && T_ID > '~',
               "a token is also a one-character token's code");
// The blocks don't overlap: the keywords start past T_STR, and the
// operators past T_KEYWORD.
_Static_assert(T_INT > T_STR && T_ADDEQ > T_KEYWORD,
               "the keywords overlap T_ID ... T_STR, or the operators the"
               " keywords");

// The current token.
extern int tok;
extern int tok_val;                 // T_NUM value; T_ID, T_KEYWORD id
extern int tok_file;                // the token's file, by number
extern int tok_unsigned;            // T_NUM: it is unsigned (a u suffix,
                                    // or hex or octal over $7FFF)
extern char tok_text[MAX_STR + 1];  // T_STR bytes
extern int tok_len;                 // T_STR length, not counting a NUL
extern int tok_hsum, tok_hmul;      // T_ID after T_STRUCT: the tag's
                                    // spelling's hash (CCPP.md)

void tok_open(const char *base);    // NAME.i and NAME.nam; the first token
void tok_close(void);
void next(void);                    // advance to the next token
int  next_is_rbracket(void);        // is the token after this one ']'?
int  next_is_colon(void);           // is the token after this one ':'?
#ifdef CCISH_DIAG
const char *tok_desc(void);         // the current token, for messages
#else
#define tok_desc() ((const char *)0)
#endif

#endif
