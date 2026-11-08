/*
 * cccomp's token reader (tokin.c): the tokens of NAME.i, which ccpp
 * wrote (../CCPP.md), one at a time, as the parser's T_* tokens.
 * Include after cclimits.h.
 */
#ifndef TOKIN_H
#define TOKIN_H

/* Single-character tokens are their character code; others start at
 * 256. T_INT ... T_TYPEDEF are in the order of the token stream's
 * keyword codes, and T_EQ ... T_ELLIPSIS of its operator codes
 * (token.h). The type-name keywords are grouped, so that the T_IS_*
 * sets below are ranges. */
enum {
  T_EOF = 0,
  T_ID = 256, T_NUM, T_STR,
  /* keywords supported now. The type-name keywords first, in the
   * groups the parser tests: the int-type specifiers, then void, the
   * qualifiers, the tagged types */
  T_INT, T_CHAR, T_SIGNED, T_UNSIGNED,
  T_VOID,
  T_CONST, T_VOLATILE,
  T_STRUCT, T_ENUM,
  /* the other keywords: the control-flow ones, then the rest, with
   * the storage-class keywords and the switch keywords each kept
   * adjacent */
  T_RETURN, T_IF, T_ELSE, T_WHILE, T_DO, T_FOR, T_BREAK, T_CONTINUE,
  T_SIZEOF,
  T_STATIC, T_EXTERN,
  T_SWITCH, T_CASE, T_DEFAULT,
  T_TYPEDEF,
  T_GOTO,
  /* union takes the first spare supported-keyword code, so a new
   * keyword takes one of the rest instead of renumbering T_KEYWORD
   * and the operators (ccpp never writes a spare: NKEYWORDS stops
   * below them) */
  T_UNION, T_KW_SPARE1, T_KW_SPARE2,
  /* any other C keyword: reported as "not supported yet" */
  T_KEYWORD,
  /* multi-character operators (not all are supported by the parser yet) */
  T_EQ, T_NE, T_LE, T_GE, T_SHL, T_SHR, T_ANDAND, T_OROR, T_INC, T_DEC,
  T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ, T_MODEQ, T_ANDEQ, T_OREQ, T_XOREQ,
  T_SHLEQ, T_SHREQ, T_ARROW, T_ELLIPSIS
};

/* The sets of tokens the parser tests, each a range in the order
 * above. A new token joins a group by taking a place in it; union,
 * whose stream code fixed its place past the ranges, joins by name. */
#define T_IS_TYPE_START(t)  (((t) >= T_INT && (t) <= T_ENUM) || (t) == T_UNION)
#define T_IS_INT_TYPE(t)    ((t) >= T_INT && (t) <= T_UNSIGNED)
#define T_IS_QUAL(t)        ((t) >= T_CONST && (t) <= T_VOLATILE)
#define T_IS_TAGGED(t)      (((t) >= T_STRUCT && (t) <= T_ENUM) || (t) == T_UNION)
#define T_IS_STORAGE(t)     ((t) >= T_STATIC && (t) <= T_EXTERN)
#define T_IS_CASE_LABEL(t)  ((t) >= T_CASE && (t) <= T_DEFAULT)

/* The current token. */
extern int tok;
extern int tok_val;                 /* T_NUM value; T_ID, T_KEYWORD id */
extern int tok_file;                /* the token's file, by number */
extern int tok_unsigned;            /* T_NUM: it is unsigned (a u suffix,
                                     * or hex or octal over $7FFF) */
extern char tok_text[MAX_STR + 1];  /* T_STR bytes */
extern int tok_len;                 /* T_STR length, not counting a NUL */
extern int tok_hsum, tok_hmul;      /* T_ID after T_STRUCT: the tag's
                                     * spelling's hash (CCPP.md) */

void tok_open(const char *base);    /* NAME.i and NAME.nam; the first token */
void tok_close(void);
void next(void);                    /* advance to the next token */
int  next_is_rbracket(void);        /* is the token after this one ']'? */
int  next_is_colon(void);           /* is the token after this one ':'? */
#ifdef CCISH_DIAG
const char *tok_desc(void);         /* the current token, for messages */
#else
#define tok_desc() ((const char *)0)
#endif

#endif
