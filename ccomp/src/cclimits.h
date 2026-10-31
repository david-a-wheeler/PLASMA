/*
 * The limits every one of ccish's programs shares (approach.md, "Table
 * sizes").
 */
#ifndef CCLIMITS_H
#define CCLIMITS_H

#define MAX_NAME     31     /* significant characters in an identifier */
#define MAX_PATH     128    /* a file name's characters, with its NUL: a
                             * ProDOS path has at most 64, or 128 with a
                             * prefix (diag.c's buffers) */

#endif
