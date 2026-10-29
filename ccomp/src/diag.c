/*
 * Diagnostics and other support shared by ccish's programs: ccish
 * (both passes), cccomp (the compile pass) and cclink (the final pass;
 * approach.md, "B3"). Each program supplies clean_up(), which closes
 * and removes its partial output after an error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccish.h"

const char *src_name = "";
int src_line;
int opt_list;
int opt_ir;
int opt_keep;

/* Return base followed by suffix, in a static buffer that the next
 * call reuses. */
const char *stream_name(const char *base, const char *suffix)
{
    static char buf[300];
    if (strlen(base) + strlen(suffix) >= sizeof(buf))
        fatal("output name too long", NULL);
    strcpy(buf, base);
    strcat(buf, suffix);
    return buf;
}

/* Remove the stream files of output name base, unless -k asked to keep
 * them. */
void remove_streams(const char *base)
{
    if (!base || opt_keep)
        return;
    remove(stream_name(base, SUFFIX_DATA));
    remove(stream_name(base, SUFFIX_DREL));
    remove(stream_name(base, SUFFIX_STRS));
    remove(stream_name(base, SUFFIX_CODE));
    remove(stream_name(base, SUFFIX_SYMS));
}

/* The output name for source file src: its name without ".c", in a
 * static buffer. */
const char *base_of(const char *src)
{
    static char buf[256];
    size_t n = strlen(src);
    if (n < 3 || strcmp(src + n - 2, ".c") || n - 2 >= sizeof(buf))
        fatal("input name must end in .c, or use -o", NULL);
    memcpy(buf, src, n - 2);
    buf[n - 2] = 0;
    return buf;
}

/* Print a diagnostic to stderr, with the source file and line when
 * there is one. */
static void report(const char *kind, const char *msg, const char *detail)
{
    fflush(stdout);
    if (src_line > 0)
        fprintf(stderr, "%s:%d: %s: %s", src_name, src_line, kind, msg);
    else
        fprintf(stderr, "ccish: %s: %s", kind, msg);
    if (detail)
        fprintf(stderr, " %s", detail);
    fprintf(stderr, "\n");
}

/* Report an error in the program being compiled, remove partial
 * output, and exit. Compilation stops at the first error. */
void error(const char *msg, const char *detail)
{
    report("error", msg, detail);
    clean_up();
    exit(1);
}

/* Report a likely mistake in the program being compiled that C still
 * allows, as gcc would warn about it (such as discarding const), and
 * go on compiling. */
void warning(const char *msg, const char *detail)
{
    report("warning", msg, detail);
}

/* Report a problem that isn't the program's fault (a compiler limit or
 * a file error), remove partial output, and exit. */
void fatal(const char *msg, const char *detail)
{
    report("fatal", msg, detail);
    clean_up();
    exit(1);
}
