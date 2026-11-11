// The token stream (NAME.i) and the name file (NAME.nam), as ccpp
// writes them and cccomp reads them: see ../CCPP.md. Changing a code
// here changes TOK_VERSION.
#ifndef TOKEN_H
#define TOKEN_H

// ENUM VALUES: the codes below are the NAME.i format (CCPP.md),
// changed with it and its version (above); the set tests on them
// here are each a _Static_assert, and the codes may be reassigned
// freely as long as those hold. See ../doc/enums.md.

#define SUFFIX_I     ".i"
#define SUFFIX_NAM   ".nam"
#define TOK_VERSION  8        // NAME.i's format, after its "CTK"
#define NAM_VERSION  2        // NAME.nam's, in its stamp line

// The codes of NAME.i. A one-character token ($20-$7E) is itself.
// TC_ID through TC_KEYWORD are the tokens that carry a 16-bit value
// in the stream (TC_IS_WORDVAL, below).
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
  TC_KEYWORDS = 0x80      // the supported keywords, in CCPP.md's order
};

// The tokens that carry a 16-bit value in the stream, their bytes
// the code plus the value's low and high bytes (CCPP.md).
#define TC_IS_WORDVAL(c)   ((c) >= TC_ID && (c) <= TC_KEYWORD)
_Static_assert(TC_IS_WORDVAL(TC_ID) && TC_IS_WORDVAL(TC_NUM)
               && TC_IS_WORDVAL(TC_UNUM) && TC_IS_WORDVAL(TC_KEYWORD)
               && TC_KEYWORD - TC_ID == 3,
               "TC_IS_WORDVAL is exactly TC_ID, TC_NUM, TC_UNUM and TC_KEYWORD");
// A one-character token is itself, $20-$7E, so no other code may be.
_Static_assert(TC_TAG < ' ' && TC_KEYWORDS > '~',
               "a token code is among the one-character tokens' ($20-$7E)");

// The multi-character operators' codes, in CCPP.md's order (and
// pplex.c's ops[], and T_ADDEQ's: tokin.h has why, and the blocks),
// TC_OPS up; UNUSED codes are spare, and have a spelling of " ".
enum {
  TC_ADDEQ = 0xC0, TC_SUBEQ, TC_MULEQ, TC_ANDEQ, TC_OREQ, TC_XOREQ,
  TC_DIVEQ = TC_ADDEQ + 8, TC_MODEQ, TC_SHLEQ, TC_SHREQ, // $C6-$C7, $CC-$CF:
                                                  // UNUSED
  TC_INC = TC_ADDEQ + 0x10, TC_DEC, TC_ANDAND, TC_OROR,
  TC_EQ, TC_NE, TC_LE, TC_GE, TC_SHL, TC_SHR, TC_ARROW, TC_ELLIPSIS
                                                  // $DC-$DF: UNUSED
};
#define TC_OPS       TC_ADDEQ // the first operator's code
#define NOPS         32       // operator codes, UNUSED ones included

// The supported keywords' codes, in CCPP.md's order (and pplex.c's
// keywords[], and T_INT's: tokin.h has why, and the blocks); UNUSED
// codes are spare, and have a spelling of " ".
enum {
  TC_INT = TC_KEYWORDS, TC_CHAR, TC_SIGNED, TC_UNSIGNED,
  TC_CONST, TC_VOLATILE,                          // $86-$87: UNUSED
  TC_STRUCT = TC_INT + 0x08, TC_UNION, TC_ENUM,   // $8B: UNUSED
  TC_VOID = TC_INT + 0x0C,                        // $8D-$8F: UNUSED
  TC_STATIC = TC_INT + 0x10, TC_EXTERN, TC_REGISTER, // $93: UNUSED
  TC_TYPEDEF = TC_INT + 0x14, TC_CONSTEXPR, TC_CASE, TC_DEFAULT,
  TC_RETURN, TC_IF, TC_ELSE, TC_WHILE, TC_DO, TC_FOR, TC_BREAK, TC_CONTINUE,
  TC_SIZEOF, TC_SWITCH, TC_GOTO, TC_STATIC_ASSERT // $A4-$A7: UNUSED
};
#define NKEYWORDS    40       // keyword codes, UNUSED ones included

// struct or union, after which an identifier is a tag (TC_TAG), and
// && or ||, which #if evaluates lazily: aligned pairs, as tokin.h's.
#define TC_IS_STRUCT_UNION(c) IN_BLOCK(c, TC_STRUCT, 2)
#define TC_IS_LOGICAL(c)      IN_BLOCK(c, TC_ANDAND, 2)
_Static_assert((TC_STRUCT & 1) == 0 && TC_IS_STRUCT_UNION(TC_UNION)
               && (TC_ANDAND & 1) == 0 && TC_IS_LOGICAL(TC_OROR),
               "TC_IS_STRUCT_UNION or TC_IS_LOGICAL isn't an aligned pair");
// The keywords' codes, TC_KEYWORDS up, end before the operators', and
// those fit in a byte; each block's last code is inside it.
_Static_assert(TC_KEYWORDS + NKEYWORDS <= TC_OPS
               && TC_STATIC_ASSERT < TC_KEYWORDS + NKEYWORDS,
               "the keywords' codes run into the operators', or past"
               " NKEYWORDS");
_Static_assert(TC_OPS + NOPS - 1 <= 0xFF && TC_ELLIPSIS < TC_OPS + NOPS,
               "the operators' codes aren't bytes, or run past NOPS");

#endif
