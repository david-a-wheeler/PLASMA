// The token stream (NAME.i) and the name file (NAME.nam), as ccpp
// writes them and cccomp reads them: see ../CCPP.md. Changing a code
// here changes TOK_VERSION. cccomp's parser uses the codes as its
// tokens (tok, tokin.h), so the set tests on them are here too.
#ifndef TOKEN_H
#define TOKEN_H

// ENUM VALUES: the codes below are the NAME.i format (CCPP.md),
// changed with it and its version (above). They're assigned for the
// set tests below (and a few file-local ones in the .c files, each
// with a _Static_assert of what it relies on), and may be reassigned
// freely as long as those hold; never test or transform one with a
// raw number outside such a define and assert pair. See
// ../doc/enums.md.

#define SUFFIX_I     ".i"
#define SUFFIX_NAM   ".nam"
#define TOK_VERSION  8        // NAME.i's format, after its "CTK"
#define NAM_VERSION  2        // NAME.nam's, in its stamp line

// The codes of NAME.i, one byte each. A one-character token ($20-$7E)
// is itself. The parser's tokens are these codes: TC_END at the end,
// TC_ID (a TC_TAG is one too), TC_NUM (so is a TC_UNUM, with
// tok_unsigned), TC_STR, TC_KEYWORD, the keywords, the operators, and
// the one-character tokens; tokin.c reads TC_NL, TC_LINE and TC_FILE.
//
// The keywords and the operators are in CCPP.md's order (and
// pplex.c's keywords[] and ops[]), in blocks, each a set the parser
// tests: a power of 2 in size, starting at a multiple of its size, so
// that ccgen tests it with one AND and one compare (IN_BLOCK,
// build.h), as the checks below require. A block's spare codes are
// marked UNUSED: room for a new token of its kind (they're not names,
// which would cost cccomp MAX_IDS entries); each has a spelling of " ".
// The keywords, TC_KEYWORDS up:
// $00-$0F, the type-name keywords (TC_IS_TYPE_START): the int-type
// specifiers (TC_IS_INT_TYPE), then the qualifiers (TC_IS_QUAL), the
// two a block of 8 (TC_IS_INT_OR_QUAL); the tagged types
// (TC_IS_TAGGED), struct and union first (TC_IS_STRUCT_UNION); void.
// $10-$17: the storage classes (TC_IS_STORAGE), typedef and constexpr
// (TC_IS_ID_DECL), case and default (TC_IS_CASE_LABEL).
// $18-$27: the other keywords.
// The multi-character operators, TC_OPS up; not all are supported by
// the parser yet:
// $00-$0F, the compound assignments (TC_IS_ASSIGN_OP), in the order of
// the binary IR ops they do (pexpr.c's TC_ASSIGN_IR() relies on it).
// $10-$1F: ++ and -- (TC_IS_INCDEC), in IR_ADD and IR_SUB's order
// (pexpr.c's TC_INCDEC_IR()), && and || (TC_IS_LOGICAL), then the rest.
enum {
  TC_END = 0x00,          // then the stamp: id count, checksum (words)
  TC_NL = 0x01,           // the next line
  TC_LINE = 0x02,         // a line (word)
  TC_FILE = 0x03,         // a file number (byte) and a line (word)
  TC_ID = 0x04,           // an id (word)
  TC_NUM = 0x05,          // an int (word)
  TC_UNUM = 0x06,         // an unsigned (word)
  TC_KEYWORD = 0x07,      // an unsupported keyword's id (word)
  TC_STR = 0x08,          // a length (byte), then the bytes
  TC_TAG = 0x09,          // a struct or union tag: id, its spelling's
                          // hash and 31^length (words)
                          // $10-$1F: ccpp's own (TC_IS_CCPP_OWN)
  TC_KEYWORDS = 0x80,     // the supported keywords:
  TC_INT = TC_KEYWORDS, TC_CHAR, TC_SIGNED, TC_UNSIGNED,
  TC_CONST, TC_VOLATILE,          // +$06-$07: UNUSED (TC_IS_QUAL's 4)
  TC_STRUCT = TC_INT + 0x08, TC_UNION, TC_ENUM,
                                  // +$0B: UNUSED (TC_IS_TAGGED's 4)
  TC_VOID = TC_INT + 0x0C,        // +$0D-$0F: UNUSED (TC_IS_TYPE_START's)
  TC_STATIC = TC_INT + 0x10, TC_EXTERN, TC_REGISTER,
                                  // +$13: UNUSED (TC_IS_STORAGE's 4)
  TC_TYPEDEF = TC_INT + 0x14, TC_CONSTEXPR, TC_CASE, TC_DEFAULT,
  TC_RETURN, TC_IF, TC_ELSE, TC_WHILE, TC_DO, TC_FOR, TC_BREAK, TC_CONTINUE,
  TC_SIZEOF, TC_SWITCH, TC_GOTO, TC_STATIC_ASSERT,
                                  // +$24-$27: UNUSED (NKEYWORDS)
  TC_ADDEQ = 0xC0, TC_SUBEQ, TC_MULEQ, TC_ANDEQ, TC_OREQ, TC_XOREQ,
                                  // +$06-$07: UNUSED, as IR's $36-$37
  TC_DIVEQ = TC_ADDEQ + 0x08, TC_MODEQ, TC_SHLEQ, TC_SHREQ,
                                  // +$0C-$0F: UNUSED (TC_IS_ASSIGN_OP's)
  TC_INC = TC_ADDEQ + 0x10, TC_DEC, TC_ANDAND, TC_OROR,
  TC_EQ, TC_NE, TC_LE, TC_GE, TC_SHL, TC_SHR, TC_ARROW, TC_ELLIPSIS
                                  // +$1C-$1F: UNUSED (NOPS)
};
#define NKEYWORDS    40       // keyword codes, UNUSED ones included
#define TC_OPS       TC_ADDEQ // the first operator's code
#define NOPS         32       // operator codes, UNUSED ones included

// ccpp's own codes (pplex.h), for macros: never in NAME.i, but in a
// macro's body beside the stream's, so this block holds no stream
// code and no one-character token.
#define TC_IS_CCPP_OWN(c)  IN_BLOCK(c, 0x10, 16)
_Static_assert(!TC_IS_CCPP_OWN(TC_END) && !TC_IS_CCPP_OWN(TC_NL)
               && !TC_IS_CCPP_OWN(TC_LINE) && !TC_IS_CCPP_OWN(TC_FILE)
               && !TC_IS_CCPP_OWN(TC_ID) && !TC_IS_CCPP_OWN(TC_NUM)
               && !TC_IS_CCPP_OWN(TC_UNUM) && !TC_IS_CCPP_OWN(TC_KEYWORD)
               && !TC_IS_CCPP_OWN(TC_STR) && !TC_IS_CCPP_OWN(TC_TAG)
               && !TC_IS_CCPP_OWN(' ') && !TC_IS_CCPP_OWN(TC_KEYWORDS),
               "TC_IS_CCPP_OWN holds a stream code or a one-character token");

// The tokens that carry a 16-bit value in the stream, their bytes
// the code plus the value's low and high bytes (CCPP.md).
#define TC_IS_WORDVAL(c)   IN_BLOCK(c, TC_ID, 4)
_Static_assert((TC_ID & 3) == 0 && TC_IS_WORDVAL(TC_ID)
               && TC_IS_WORDVAL(TC_NUM) && TC_IS_WORDVAL(TC_UNUM)
               && TC_IS_WORDVAL(TC_KEYWORD),
               "TC_IS_WORDVAL misses a member, or isn't an aligned block");
_Static_assert(!TC_IS_WORDVAL(TC_FILE) && !TC_IS_WORDVAL(TC_STR)
               && !TC_IS_WORDVAL(TC_TAG),
               "TC_IS_WORDVAL holds a non-member");

// Is token t a number or a name: an index simple enough to need no
// code before it (pexpr.c's parse_index())?
#define TC_IS_NUM_OR_ID(t) IN_BLOCK(t, TC_ID, 2)
_Static_assert((TC_ID & 1) == 0 && TC_IS_NUM_OR_ID(TC_ID)
               && TC_IS_NUM_OR_ID(TC_NUM),
               "TC_IS_NUM_OR_ID misses a member, or isn't an aligned pair");
_Static_assert(!TC_IS_NUM_OR_ID(TC_FILE) && !TC_IS_NUM_OR_ID(TC_UNUM)
               && !TC_IS_NUM_OR_ID(TC_KEYWORD) && !TC_IS_NUM_OR_ID(TC_STR)
               && !TC_IS_NUM_OR_ID(TC_END),
               "TC_IS_NUM_OR_ID holds a non-member");

// The keywords' sets, each a block (above).
#define TC_IS_TYPE_START(t)  IN_BLOCK(t, TC_INT, 16)
#define TC_IS_INT_OR_QUAL(t) IN_BLOCK(t, TC_INT, 8)
#define TC_IS_INT_TYPE(t)    IN_BLOCK(t, TC_INT, 4)
#define TC_IS_INT_OR_CHAR(t) IN_BLOCK(t, TC_INT, 2)
#define TC_IS_QUAL(t)        IN_BLOCK(t, TC_CONST, 4)
#define TC_IS_TAGGED(t)      IN_BLOCK(t, TC_STRUCT, 4)
// struct or union: with members, and after which an identifier is a
// tag (TC_TAG).
#define TC_IS_STRUCT_UNION(t) IN_BLOCK(t, TC_STRUCT, 2)
#define TC_IS_STORAGE(t)     IN_BLOCK(t, TC_STATIC, 4)
// static or extern: the storage classes a for loop can't declare.
#define TC_IS_STATIC_OR_EXTERN(t) IN_BLOCK(t, TC_STATIC, 2)
// typedef or constexpr: names in the enum and typedef table (name.h).
#define TC_IS_ID_DECL(t)     IN_BLOCK(t, TC_TYPEDEF, 2)
#define TC_IS_CASE_LABEL(t)  IN_BLOCK(t, TC_CASE, 2)

// The type-name keywords: void and the int types, qualifiers, and
// tagged types, each block aligned and holding its members only.
_Static_assert((TC_INT & 15) == 0 && TC_IS_TYPE_START(TC_INT)
               && TC_IS_TYPE_START(TC_VOLATILE) && TC_IS_TYPE_START(TC_ENUM)
               && TC_IS_TYPE_START(TC_VOID),
               "TC_IS_TYPE_START misses a member, or isn't aligned");
_Static_assert(!TC_IS_TYPE_START('~') && !TC_IS_TYPE_START(TC_STATIC)
               && !TC_IS_TYPE_START(TC_TYPEDEF) && !TC_IS_TYPE_START(TC_ID),
               "TC_IS_TYPE_START holds a non-member");
_Static_assert(TC_IS_INT_OR_QUAL(TC_INT) && TC_IS_INT_OR_QUAL(TC_UNSIGNED)
               && TC_IS_INT_OR_QUAL(TC_CONST)
               && TC_IS_INT_OR_QUAL(TC_VOLATILE)
               && !TC_IS_INT_OR_QUAL(TC_STRUCT) && !TC_IS_INT_OR_QUAL(TC_VOID),
               "TC_IS_INT_OR_QUAL isn't the int types and the qualifiers");
_Static_assert(TC_IS_INT_TYPE(TC_INT) && TC_IS_INT_TYPE(TC_CHAR)
               && TC_IS_INT_TYPE(TC_SIGNED) && TC_IS_INT_TYPE(TC_UNSIGNED)
               && !TC_IS_INT_TYPE(TC_CONST) && !TC_IS_INT_TYPE(TC_VOID),
               "TC_IS_INT_TYPE isn't int, char, signed and unsigned");
_Static_assert(TC_IS_INT_OR_CHAR(TC_INT) && TC_IS_INT_OR_CHAR(TC_CHAR)
               && !TC_IS_INT_OR_CHAR(TC_SIGNED)
               && !TC_IS_INT_OR_CHAR(TC_UNSIGNED)
               && !TC_IS_INT_OR_CHAR(TC_VOID) && !TC_IS_INT_OR_CHAR('~'),
               "TC_IS_INT_OR_CHAR isn't int and char");
_Static_assert((TC_CONST & 3) == 0 && TC_IS_QUAL(TC_CONST)
               && TC_IS_QUAL(TC_VOLATILE) && !TC_IS_QUAL(TC_UNSIGNED)
               && !TC_IS_QUAL(TC_STRUCT),
               "TC_IS_QUAL isn't const and volatile, an aligned block");
_Static_assert((TC_STRUCT & 3) == 0 && TC_IS_TAGGED(TC_STRUCT)
               && TC_IS_TAGGED(TC_UNION) && TC_IS_TAGGED(TC_ENUM)
               && !TC_IS_TAGGED(TC_VOLATILE) && !TC_IS_TAGGED(TC_VOID),
               "TC_IS_TAGGED isn't struct, union and enum, an aligned block");
_Static_assert(TC_IS_STRUCT_UNION(TC_STRUCT) && TC_IS_STRUCT_UNION(TC_UNION)
               && !TC_IS_STRUCT_UNION(TC_ENUM)
               && !TC_IS_STRUCT_UNION(TC_VOLATILE),
               "TC_IS_STRUCT_UNION isn't struct and union");
// The declaration keywords: storage classes, typedef and constexpr,
// and the case labels.
_Static_assert((TC_STATIC & 3) == 0 && TC_IS_STORAGE(TC_STATIC)
               && TC_IS_STORAGE(TC_EXTERN) && TC_IS_STORAGE(TC_REGISTER)
               && !TC_IS_STORAGE(TC_VOID) && !TC_IS_STORAGE(TC_TYPEDEF),
               "TC_IS_STORAGE isn't static, extern and register, an aligned"
               " block");
_Static_assert(TC_IS_STATIC_OR_EXTERN(TC_STATIC)
               && TC_IS_STATIC_OR_EXTERN(TC_EXTERN)
               && !TC_IS_STATIC_OR_EXTERN(TC_REGISTER)
               && !TC_IS_STATIC_OR_EXTERN(TC_VOID)
               && !TC_IS_STATIC_OR_EXTERN(TC_TYPEDEF),
               "TC_IS_STATIC_OR_EXTERN isn't static and extern");
_Static_assert(TC_IS_ID_DECL(TC_TYPEDEF) && TC_IS_ID_DECL(TC_CONSTEXPR)
               && !TC_IS_ID_DECL(TC_REGISTER) && !TC_IS_ID_DECL(TC_CASE),
               "TC_IS_ID_DECL isn't typedef and constexpr");
_Static_assert(TC_IS_CASE_LABEL(TC_CASE) && TC_IS_CASE_LABEL(TC_DEFAULT)
               && !TC_IS_CASE_LABEL(TC_CONSTEXPR)
               && !TC_IS_CASE_LABEL(TC_RETURN),
               "TC_IS_CASE_LABEL isn't case and default");
// A one-character token is itself, $20-$7E, so no other code may be.
_Static_assert(TC_TAG < ' ' && TC_KEYWORDS > '~',
               "a token code is among the one-character tokens' ($20-$7E)");

// The operators' sets, each a block (above). The compound
// assignments, +=, -=, ... >>=:
#define TC_IS_ASSIGN_OP(t)   IN_BLOCK(t, TC_ADDEQ, 16)
#define TC_IS_INCDEC(t)      IN_BLOCK(t, TC_INC, 2)       // ++ --
// && or ||: the parser's logical operators, which #if evaluates
// lazily.
#define TC_IS_LOGICAL(t)     IN_BLOCK(t, TC_ANDAND, 2)
_Static_assert((TC_ADDEQ & 15) == 0 && TC_IS_ASSIGN_OP(TC_ADDEQ)
               && TC_IS_ASSIGN_OP(TC_XOREQ) && TC_IS_ASSIGN_OP(TC_DIVEQ)
               && TC_IS_ASSIGN_OP(TC_SHREQ),
               "TC_IS_ASSIGN_OP misses a member, or isn't aligned");
_Static_assert(!TC_IS_ASSIGN_OP('=') && !TC_IS_ASSIGN_OP(TC_INC)
               && !TC_IS_ASSIGN_OP(TC_EQ) && !TC_IS_ASSIGN_OP(TC_STATIC_ASSERT),
               "TC_IS_ASSIGN_OP holds a non-member");
_Static_assert(TC_IS_INCDEC(TC_INC) && TC_IS_INCDEC(TC_DEC)
               && !TC_IS_INCDEC(TC_ANDAND) && !TC_IS_INCDEC(TC_ADDEQ)
               && !TC_IS_INCDEC('+') && !TC_IS_INCDEC('-'),
               "TC_IS_INCDEC isn't ++ and --");
_Static_assert(TC_IS_LOGICAL(TC_ANDAND) && TC_IS_LOGICAL(TC_OROR)
               && !TC_IS_LOGICAL(TC_DEC) && !TC_IS_LOGICAL(TC_EQ)
               && !TC_IS_LOGICAL('&') && !TC_IS_LOGICAL('|'),
               "TC_IS_LOGICAL isn't && and ||");
// The keywords' codes, TC_KEYWORDS up, end before the operators', and
// those fit in a byte; each block's last code is inside it.
_Static_assert(TC_KEYWORDS + NKEYWORDS <= TC_OPS
               && TC_STATIC_ASSERT < TC_KEYWORDS + NKEYWORDS,
               "the keywords' codes run into the operators', or past"
               " NKEYWORDS");
_Static_assert(TC_OPS + NOPS - 1 <= 0xFF && TC_ELLIPSIS < TC_OPS + NOPS,
               "the operators' codes aren't bytes, or run past NOPS");

#endif
