/*
 * ccpp's lexer (pplex.c): the source, a line at a time, as tokens with
 * the codes of the token stream (token.h, ../CCPP.md). Include after
 * bio.h and token.h.
 */
#ifndef PPLEX_H
#define PPLEX_H

#define MAX_LINE     255    /* characters in one source line, ccpp's
                             * buffer (99) */
#define MAX_INCLUDE  3      /* #include files open at once, beside the
                             * source: PLASMA's Apple II file library
                             * allows 4 open files, and ccpp has the
                             * source and NAME.i open (2, ccish's own:
                             * a header, and <stdio.h>'s <stddef.h>) */

extern int pt_val;                  /* TC_NUM, TC_UNUM value; TC_ID,
                                     * TC_KEYWORD id */
extern unsigned char pt_text[MAX_STR + 1];  /* TC_STR bytes */
extern int pt_len;                  /* TC_STR length */
extern int pt_first;                /* the token is its line's first */
extern int pt_space;                /* blanks came before it (for #) */
extern int pt_macro;                /* TC_ID, TC_KEYWORD: names a macro */

/* ccpp's own codes, beside the token stream's, for macros: never in
 * NAME.i. In a macro's body, a parameter (with its index after it), one
 * not to be expanded first (next to ##), one after # (to be made a
 * string), and ##; in an argument, blanks between tokens (for #). */
enum { TC_PARAM = 0x10, TC_PARAM_RAW, TC_HASH_PARAM, TC_PASTE, TC_SPACE };

/* How the lexer reads (pp_mode): ordinary source; a directive's line,
 * up to its newline; or a group #ifdef leaves out. */
enum { PM_TEXT, PM_DIRECTIVE, PM_SKIP };
extern int pp_mode;

void pp_lex_open(Bfile *f);
void pp_lex_text(const char *s);    /* a -D or -U, as a directive's line */
void pp_lex_push(Bfile *f);         /* #include f, at the line's end */
void pp_lex_pop(void);              /* back from it */
int  pp_lex(void);                  /* the next token's code */
int  pp_lex_word(char *buf, int size);  /* a directive's name */
int  pp_lex_header(char *buf, int size, int *angle);  /* "f" or <f> */
int  pp_lex_next_char(void);        /* the character after the token */
const char *pp_lex_rest(void);      /* the rest of a directive's line */
void pp_lex_set_line(int n);        /* #line n */
int  pp_word(const char *word, int n);  /* a word's code, as lexed */
int  pp_op(const char *s);          /* an operator's code, or -1 */
int  pp_spell_token(int code, int val, const unsigned char *s, int n,
                    char *buf, int size);  /* as source, for # and ## */

#endif
