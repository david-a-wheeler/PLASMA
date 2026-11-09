// Diagnostics and program support (diag.c), shared by all of ccish's
// programs (approach.md, "B3", and "C2 design"). Include after
// build.h.
//
// diag.c's exports are in two headers: this one, and diag.src.h, the
// source position that messages give (src_name, src_line and
// src_name_hook). That's kept apart because every variable a file can
// see counts against cccomp's MAX_VARS when ccish compiles itself, and
// ccrel's files don't need it. Include diag.src.h after this one.
#ifndef DIAG_H
#define DIAG_H

#ifdef CCISH_LISTING
extern int opt_list;        // -l: print a listing of the generated code
#else
#define opt_list 0          // no listing in the small build
#endif
extern int opt_ir;          // -i: print the IR as ccgen reads it
#ifdef CCISH_LISTING
extern int opt_dump;        // -d: print the machine ops after peephole
                            // (full build only)
#endif
extern int opt_keep;        // -k: keep the stream files

// Diagnostics take a message number: its text is in the message file,
// ccish.msg, and is also written, in double quotes, in a comment right
// after the number wherever it is used. A part is another message
// number, printed after the text (0 for none).
void error(int msg, const char *detail);
void error0(int msg);                       // error(msg, NULL)
void error_in(int msg, int part, const char *detail);
void fatal(int msg, const char *detail);
void fatal0(int msg);                       // fatal(msg, NULL)
#ifdef CCISH_DIAG
void warning(int msg, const char *detail);  // only cccomp's, in the
void warning_in(int msg, int part, const char *detail); // full build
#endif
void msg_init(const char *argv0);           // find ccish.msg
int  msg_text(int n, char *buf, int size);  // message n's text, or 0
const char *stream_name(const char *base, const char *suffix);
void remove_stream(const char *base, const char *suffix);  // unless -k
const char *num_text(int n);                // n in decimal (no printf)

// Defined by each program: after an error, close the open files and
// remove partial output.
void clean_up(void);

#endif
