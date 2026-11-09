// The token stream (NAME.i) and the name file (NAME.nam), as ccpp
// writes them and cccomp reads them: see ../CCPP.md. Changing a code
// here changes TOK_VERSION.
#ifndef TOKEN_H
#define TOKEN_H

#define SUFFIX_I     ".i"
#define SUFFIX_NAM   ".nam"
#define TOK_VERSION  6        // NAME.i's format, after its "CTK"
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

// The multi-character operators, in CCPP.md's order (and T_EQ's).
enum {
  TC_EQ = 0xC0, TC_NE, TC_LE, TC_GE, TC_SHL, TC_SHR, TC_ANDAND, TC_OROR,
  TC_INC, TC_DEC, TC_ADDEQ, TC_SUBEQ, TC_MULEQ, TC_DIVEQ, TC_MODEQ,
  TC_ANDEQ, TC_OREQ, TC_XOREQ, TC_SHLEQ, TC_SHREQ, TC_ARROW, TC_ELLIPSIS
};

// The supported keywords' codes, in CCPP.md's order (and pplex.c's
// keywords[], and T_INT's).
enum {
  TC_INT = TC_KEYWORDS, TC_CHAR, TC_SIGNED, TC_UNSIGNED, TC_VOID, TC_CONST,
  TC_VOLATILE, TC_STRUCT, TC_ENUM, TC_RETURN, TC_IF, TC_ELSE, TC_WHILE,
  TC_DO, TC_FOR, TC_BREAK, TC_CONTINUE, TC_SIZEOF, TC_STATIC, TC_EXTERN,
  TC_SWITCH, TC_CASE, TC_DEFAULT, TC_TYPEDEF, TC_GOTO, TC_UNION,
  TC_STATIC_ASSERT, TC_CONSTEXPR
};
#define NKEYWORDS    (TC_CONSTEXPR - TC_KEYWORDS + 1)   // supported keywords
#define NOPS         22       // operators: == ... "..."
// The keywords' codes, TC_KEYWORDS up, end before the operators'
// ($80-$BF), and the operators' fit in a byte ($C0-$DF): CCPP.md.
_Static_assert(TC_KEYWORDS + NKEYWORDS <= TC_EQ && TC_EQ == 0xC0,
               "the keywords' codes run into the operators'");
_Static_assert(TC_ELLIPSIS - TC_EQ == NOPS - 1 && TC_ELLIPSIS <= 0xDF,
               "NOPS isn't the number of operators, TC_EQ ... TC_ELLIPSIS");

#endif
