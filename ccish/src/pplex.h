// ccpp's lexer (pplex.c): the source, a line at a time, as tokens with
// the codes of the token stream (token.h, ../CCPP.md). Include after
// cclimits.h, bio.h and token.h.
#ifndef PPLEX_H
#define PPLEX_H

// ENUM VALUES: assigned here, and the TC_* codes are the NAME.i
// format (CCPP.md); a set test or value manipulation on them is a
// #define with a _Static_assert (next to the values, or file-local
// in its only user), and the values may be reassigned freely as
// long as those checks hold. See ../doc/enums.md.

#define MAX_LINE     255    // limit pp: characters in one source line, after
                            // joining the lines that end in a backslash:
                            // ccpp's buffer
#define MAX_INCLUDE  3      // limit pp: #include files open at once, beside
                            // the source: PLASMA's Apple II file library
                            // allows 4 open files, and ccpp has the source
                            // and NAME.i open

extern int pt_val;                  // TC_NUM, TC_UNUM value; TC_ID,
                                    // TC_KEYWORD id
extern unsigned char pt_text[MAX_STR + 1];  // TC_STR bytes
extern int pt_len;                  // TC_STR length
extern int pt_first;                // the token is its line's first
extern int pt_space;                // blanks came before it (for #)
extern int pt_macro;                // TC_ID, TC_KEYWORD: names a macro

// ccpp's own codes, beside the token stream's, for macros: never in
// NAME.i. In a macro's body, a parameter (with its index after it), one
// not to be expanded first (next to ##), one after # (to be made a
// string), and ##; in an argument, blanks between tokens (for #).
enum { TC_PARAM = 0x10, TC_PARAM_RAW, TC_HASH_PARAM, TC_PASTE, TC_SPACE };

// The three parameter tokens, in the enum's order (ppmac.c): a
// parameter, one not to be expanded first, one after #.
#define TC_IS_PARAM(c)       ((c) >= TC_PARAM && (c) <= TC_HASH_PARAM)
_Static_assert(TC_IS_PARAM(TC_PARAM) && TC_IS_PARAM(TC_PARAM_RAW)
               && TC_IS_PARAM(TC_HASH_PARAM) && TC_HASH_PARAM - TC_PARAM == 2,
               "TC_IS_PARAM is exactly the three parameter tokens");
// ccpp's codes share a macro body with the stream's (token.h), so they
// must differ from those, and from the one-character tokens. (The
// casts: gcc warns about comparing two enums' constants.)
_Static_assert((int)TC_PARAM > (int)TC_TAG && TC_SPACE < ' ',
               "a ccpp code is also a token stream code");

// How the lexer reads (pp_mode): ordinary source; a directive's line,
// up to its newline; or a group #ifdef leaves out.
enum { PM_TEXT, PM_DIRECTIVE, PM_SKIP };
extern int pp_mode;

void pp_lex_open(Bfile *f);
void pp_lex_text(const char *s);    // a -D or -U, as a directive's line
void pp_lex_push(Bfile *f);         // #include f, at the line's end
void pp_lex_pop(void);              // back from it
int  pp_lex(void);                  // the next token's code
int  pp_lex_word(char *buf, int size);  // a directive's name
int  pp_lex_header(char *buf, int size, int *angle);  /* "f" or <f> */
int  pp_lex_next_char(void);        // the character after the token
const char *pp_lex_rest(void);      // the rest of a directive's line
void pp_lex_set_line(int n);        // #line n
int  pp_word(const char *word, int n);  // a word's code, as lexed
int  pp_op(const char *s);          // an operator's code, or -1
int  pp_spell_token(int code, int val, const unsigned char *s, int n,
                    char *buf, int size);  // as source, for # and ##

#endif
