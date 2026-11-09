/*
 * ccish's object format (../OBJECT.md): the record tags of NAME.obj
 * and of the IR file (one set of letters), and the helpers, for the
 * files that write or read them. Changing the format changes
 * OBJ_VERSION. The relocations and symbol flags are in obj.sym.h, and
 * the limits and file names in obj.h.
 */
#ifndef OBJ_FMT_H
#define OBJ_FMT_H

#define OBJ_VERSION 7       /* the format version, in the symbol summary */

/* Record tags in the stream (see ../OBJECT.md), as letters so that a
 * dump of the file is easy to read. */
enum { REC_DATA = 'D', REC_DREL = 'R', REC_STR = 'S', REC_FUNC = 'F',
       REC_HELPER = 'H', REC_ZERO = 'Z' };

/* The IR file's own record tags, besides the stream's D, R, S and Z
 * (above), which it uses too. In the IR file every record tag is in
 * lowercase (ir.h, IR_REC()). */
enum { REC_LINE = 'L', REC_NAME = 'N', REC_WORD = 'W', REC_SECOND = 'X',
       REC_SUMMARY = 'Y' };

/* A record the back end copies to the stream as it is (irin.c):
 * data, a relocation, a string, or zero-filled bytes. */
#define REC_IS_PASSTHROUGH(t)  ((t) == REC_DATA || (t) == REC_DREL \
                                || (t) == REC_STR || (t) == REC_ZERO)

/* The helper functions an object may hold (approach.md, "unsigned and
 * helper functions design"), by number. */
enum { HELP_UDIV, HELP_UMOD, HELP_USHR, NHELPERS };

#endif
