/*
 * cccomp's token reader (tokin.c): the tokens of NAME.i, which ccpp
 * wrote (../CCPP.md), one at a time, as the parser's T_* tokens.
 * Include after token.h.
 */
#ifndef TOKIN_H
#define TOKIN_H

/* Single-character tokens are their character code; others start at
 * 256. T_INT ... T_UNSIGNED are in the order of the token stream's
 * keyword codes, and T_EQ ... T_ELLIPSIS of its operator codes
 * (token.h). */
enum {
  T_EOF = 0,
  T_ID = 256, T_NUM, T_STR,
  /* keywords supported now */
  T_INT, T_VOID, T_RETURN, T_IF, T_ELSE, T_WHILE, T_DO, T_FOR, T_BREAK,
  T_CONTINUE, T_CHAR, T_SIZEOF, T_STATIC, T_CONST, T_VOLATILE, T_STRUCT,
  T_SWITCH, T_CASE, T_DEFAULT, T_TYPEDEF, T_ENUM, T_EXTERN, T_SIGNED,
  T_UNSIGNED,
  /* any other C keyword: reported as "not supported yet" */
  T_KEYWORD,
  /* multi-character operators (not all are supported by the parser yet) */
  T_EQ, T_NE, T_LE, T_GE, T_SHL, T_SHR, T_ANDAND, T_OROR, T_INC, T_DEC,
  T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ, T_MODEQ, T_ANDEQ, T_OREQ, T_XOREQ,
  T_SHLEQ, T_SHREQ, T_ARROW, T_ELLIPSIS
};

/* The current token. */
extern int tok;
extern int tok_val;                 /* T_NUM value; T_ID, T_KEYWORD id */
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
const char *tok_desc(void);         /* the current token, for messages */

#endif
