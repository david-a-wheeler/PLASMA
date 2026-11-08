/*
 * The token stream (NAME.i) and the name file (NAME.nam), as ccpp
 * writes them and cccomp reads them: see ../CCPP.md. Changing a code
 * here changes TOK_VERSION.
 */
#ifndef TOKEN_H
#define TOKEN_H

#define SUFFIX_I     ".i"
#define SUFFIX_NAM   ".nam"
#define TOK_VERSION  4        /* NAME.i's format, after its "CTK" */
#define NAM_VERSION  2        /* NAME.nam's, in its stamp line */

/* The codes of NAME.i. A one-character token ($20-$7E) is itself.
 * TC_ID through TC_KEYWORD are the tokens that carry a 16-bit value
 * in the stream (TC_IS_WORDVAL, below). */
enum {
  TC_END = 0x00,          /* then the stamp: id count, checksum (words) */
  TC_NL = 0x01,           /* the next line */
  TC_LINE = 0x02,         /* a line (word) */
  TC_FILE = 0x03,         /* a file number (byte) and a line (word) */
  TC_ID = 0x04,           /* an id (word) */
  TC_NUM = 0x05,          /* an int (word) */
  TC_UNUM = 0x06,         /* an unsigned (word) */
  TC_KEYWORD = 0x07,      /* an unsupported keyword's id (word) */
  TC_STR = 0x08,          /* a length (byte), then the bytes */
  TC_TAG = 0x09,          /* a struct or union tag: id, its spelling's
                           * hash and 31^length (words) */
  TC_KEYWORDS = 0x80      /* the supported keywords, in CCPP.md's order */
};

/* The tokens that carry a 16-bit value in the stream, their bytes
 * the code plus the value's low and high bytes (CCPP.md). */
#define TC_IS_WORDVAL(c)   ((c) >= TC_ID && (c) <= TC_KEYWORD)

/* The multi-character operators, in CCPP.md's order (and T_EQ's). */
enum {
  TC_EQ = 0xC0, TC_NE, TC_LE, TC_GE, TC_SHL, TC_SHR, TC_ANDAND, TC_OROR,
  TC_INC, TC_DEC, TC_ADDEQ, TC_SUBEQ, TC_MULEQ, TC_DIVEQ, TC_MODEQ,
  TC_ANDEQ, TC_OREQ, TC_XOREQ, TC_SHLEQ, TC_SHREQ, TC_ARROW, TC_ELLIPSIS
};

#define TC_STRUCT    (TC_KEYWORDS + 7)    /* struct, for TC_TAG */
#define TC_UNION     (TC_KEYWORDS + 25)   /* union, for TC_TAG */
#define NKEYWORDS    26       /* supported keywords: int ... union */
#define NOPS         22       /* operators: == ... "..." */

#endif
