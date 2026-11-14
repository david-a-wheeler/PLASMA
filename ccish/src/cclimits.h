// The limits every one of ccish's programs shares (approach.md, "Table
// sizes").
#ifndef CCLIMITS_H
#define CCLIMITS_H

#define MAX_NAME     31     // limit other: significant characters in an
                            // identifier; the rest are ignored
#define MAX_STR      255    // limit other: bytes in one string literal, after
                            // adjacent ones are joined, so that it is a
                            // PLASMA string too
#define MAX_PATH     128    // limit other: a file name's characters, with its
                            // NUL: a ProDOS path has at most 64, or 128 with
                            // a prefix (diag.c's buffers)

#endif
