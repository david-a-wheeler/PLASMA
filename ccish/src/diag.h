/*
 * Diagnostics and program support (diag.c), shared by all of ccish's
 * programs (approach.md, "B3", and "C2 design"). Include after
 * build.h.
 */
#ifndef DIAG_H
#define DIAG_H

extern const char *src_name;
extern void (*src_name_hook)(void);  /* if set, fills in src_name when a
                                      * message needs it (tokin.c) */
extern int src_line;
#ifdef CCISH_LISTING
extern int opt_list;        /* -l: print a listing of the generated code */
#else
#define opt_list 0          /* no listing in the small build */
#endif
extern int opt_ir;          /* -i: print the IR as ccgen reads it */
#ifdef CCISH_LISTING
extern int opt_dump;        /* -d: print the machine ops after peephole
                             * (full build only) */
#endif
extern int opt_keep;        /* -k: keep the stream files */

/* Diagnostics take a message number: its text is in the message file,
 * ccish.msg, and is also written, in double quotes, in a comment right
 * after the number wherever it is used. A part is another message
 * number, printed after the text (0 for none). */
void error(int msg, const char *detail);
void error_in(int msg, int part, const char *detail);
void fatal(int msg, const char *detail);
void warning(int msg, const char *detail);
void warning_in(int msg, int part, const char *detail);
void msg_init(const char *argv0);           /* find ccish.msg */
int  msg_text(int n, char *buf, int size);  /* message n's text, or 0 */
const char *stream_name(const char *base, const char *suffix);
void remove_stream(const char *base, const char *suffix);  /* unless -k */
void remove_streams(const char *base);      /* .obj and .syms, unless -k */
const char *base_of(const char *src);       /* "x.c" -> "x" */
const char *num_text(int n);                /* n in decimal (no printf) */

/* Defined by each program: after an error, close the open files and
 * remove partial output. */
void clean_up(void);

#endif
