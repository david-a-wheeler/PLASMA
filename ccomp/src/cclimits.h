/*
 * The limits every one of ccish's programs shares (approach.md, "Table
 * sizes").
 */
#ifndef CCLIMITS_H
#define CCLIMITS_H

#define MAX_NAME     31     /* significant characters in an identifier */
#define MAX_STR      255    /* bytes in one string literal, so that it
                             * is a PLASMA string too (85) */
#define MAX_PATH     128    /* a file name's characters, with its NUL: a
                             * ProDOS path has at most 64, or 128 with a
                             * prefix (diag.c's buffers) */

#endif
