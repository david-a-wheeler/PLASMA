/*
 * The source position that diag.c's messages give, for the programs
 * that read a source (ccpp, cccomp, and ccgen, which names its source).
 * Include after diag.h. It's part of diag.c's exports, apart from
 * diag.h (whose comment says why) only so that ccrel's files don't see
 * it.
 */
#ifndef DIAG_SRC_H
#define DIAG_SRC_H

extern const char *src_name;    /* the source file, for messages */
extern int src_line;            /* its line, or 0 for none */
extern void (*src_name_hook)(void);  /* if set, fills in src_name when a
                                      * message needs it (tokin.c) */

#endif
