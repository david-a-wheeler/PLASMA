// cccomp's token reader (tokin.c): the tokens of NAME.i, which ccpp
// wrote (../CCPP.md), one at a time. A token is its stream code
// (token.h's TC_*, which has the set tests on them) or a one-character
// token's character. Include after cclimits.h and token.h.
#ifndef TOKIN_H
#define TOKIN_H

// The current token.
extern int tok;
extern int tok_val;                 // TC_NUM value; TC_ID, TC_KEYWORD id
extern int tok_file;                // the token's file, by number
extern int tok_unsigned;            // TC_NUM: it is unsigned (a u suffix,
                                    // or hex or octal over $7FFF)
extern char tok_text[MAX_STR + 1];  // TC_STR bytes
extern int tok_len;                 // TC_STR length, not counting a NUL
extern int tok_hsum, tok_hmul;      // TC_ID after TC_STRUCT: the tag's
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
