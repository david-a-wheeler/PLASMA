// cccomp's token reader (tokin.c): the tokens of NAME.i, which ccpp
// wrote (../CCPP.md), one at a time, as the parser's T_* tokens.
// Include after cclimits.h.
#ifndef TOKIN_H
#define TOKIN_H

// Single-character tokens are their character code; others start at
// 256. T_INT ... T_CONSTEXPR are in the order of the token stream's
// keyword codes, and T_EQ ... T_ELLIPSIS of its operator codes
// (token.h). The type-name keywords are grouped, so that the T_IS_*
// sets below are ranges.
enum {
  T_EOF = 0,
  T_ID = 256, T_NUM, T_STR,
  // keywords supported now. The type-name keywords first, in the
  // groups the parser tests: the int-type specifiers, then void, the
  // qualifiers, the tagged types
  T_INT, T_CHAR, T_SIGNED, T_UNSIGNED,
  T_VOID,
  T_CONST, T_VOLATILE,
  T_STRUCT, T_ENUM,
  // the other keywords: the control-flow ones, then the rest, with
  // the storage-class keywords and the switch keywords each kept
  // adjacent
  T_RETURN, T_IF, T_ELSE, T_WHILE, T_DO, T_FOR, T_BREAK, T_CONTINUE,
  T_SIZEOF,
  T_STATIC, T_EXTERN,
  T_SWITCH, T_CASE, T_DEFAULT,
  T_TYPEDEF,
  T_GOTO,
  // union, _Static_assert and constexpr took the spare codes after
  // goto; the next keyword renumbers T_KEYWORD and the operators,
  // which changes only cccomp (their stream codes have room)
  T_UNION, T_STATIC_ASSERT, T_CONSTEXPR,
  // any other C keyword: reported as "not supported yet"
  T_KEYWORD,
  // multi-character operators (not all are supported by the parser yet)
  T_EQ, T_NE, T_LE, T_GE, T_SHL, T_SHR, T_ANDAND, T_OROR, T_INC, T_DEC,
  T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ, T_MODEQ, T_ANDEQ, T_OREQ, T_XOREQ,
  T_SHLEQ, T_SHREQ, T_ARROW, T_ELLIPSIS
};

// The sets of tokens the parser tests, each a range in the order
// above. A new token joins a group by taking a place in it; union,
// whose stream code fixed its place past the ranges, joins by name.
#define T_IS_TYPE_START(t)  (((t) >= T_INT && (t) <= T_ENUM) || (t) == T_UNION)
#define T_IS_INT_TYPE(t)    ((t) >= T_INT && (t) <= T_UNSIGNED)
#define T_IS_QUAL(t)        ((t) >= T_CONST && (t) <= T_VOLATILE)
#define T_IS_TAGGED(t)      (((t) >= T_STRUCT && (t) <= T_ENUM) || (t) == T_UNION)
#define T_IS_STORAGE(t)     ((t) >= T_STATIC && (t) <= T_EXTERN)
#define T_IS_CASE_LABEL(t)  ((t) >= T_CASE && (t) <= T_DEFAULT)
// Each set is exactly its members: each is in its range, and the range
// has room for no other token.
_Static_assert(T_IS_TYPE_START(T_INT) && T_IS_TYPE_START(T_CHAR)
               && T_IS_TYPE_START(T_SIGNED) && T_IS_TYPE_START(T_UNSIGNED)
               && T_IS_TYPE_START(T_VOID) && T_IS_TYPE_START(T_CONST)
               && T_IS_TYPE_START(T_VOLATILE) && T_IS_TYPE_START(T_STRUCT)
               && T_IS_TYPE_START(T_ENUM) && T_ENUM - T_INT == 8
               && T_IS_TYPE_START(T_UNION),
               "T_IS_TYPE_START isn't the int types, void, the qualifiers"
               " and the tagged types");
_Static_assert(T_IS_INT_TYPE(T_INT) && T_IS_INT_TYPE(T_CHAR)
               && T_IS_INT_TYPE(T_SIGNED) && T_IS_INT_TYPE(T_UNSIGNED)
               && T_UNSIGNED - T_INT == 3,
               "T_IS_INT_TYPE isn't int, char, signed and unsigned");
_Static_assert(T_IS_QUAL(T_CONST) && T_IS_QUAL(T_VOLATILE)
               && T_VOLATILE - T_CONST == 1,
               "T_IS_QUAL isn't const and volatile");
_Static_assert(T_IS_TAGGED(T_STRUCT) && T_IS_TAGGED(T_ENUM)
               && T_IS_TAGGED(T_UNION) && T_ENUM - T_STRUCT == 1,
               "T_IS_TAGGED isn't struct, enum and union");
_Static_assert(T_IS_STORAGE(T_STATIC) && T_IS_STORAGE(T_EXTERN)
               && T_EXTERN - T_STATIC == 1,
               "T_IS_STORAGE isn't static and extern");
_Static_assert(T_IS_CASE_LABEL(T_CASE) && T_IS_CASE_LABEL(T_DEFAULT)
               && T_DEFAULT - T_CASE == 1,
               "T_IS_CASE_LABEL isn't case and default");
// A one-character token is its character code, so the others start
// past them.
_Static_assert(T_EOF == 0 && T_ID > 0xFF,
               "a token is also a one-character token's code");

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
