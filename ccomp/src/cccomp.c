/*
 * cccomp: ccish's compile pass as a program of its own (approach.md,
 * "B3"). It compiles one .c file into the stream files, and leaves
 * them for ccrel, the final pass, to turn into a module. On the Apple
 * II, a small driver runs cccomp and then ccrel, and each is freed
 * when it ends, so each has nearly all of memory.
 *
 * Usage: cccomp [-i] [-k] [-o output] input.c
 *   -i  print the IR stream between stages 1 and 2
 *   -k  keep the stream files even after an error
 *   -o  output name for the stream files (default: input name without
 *       ".c")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccish.h"

/* The stream files' output name, to remove them after an error. */
static const char *base_name;

/* After an error: close the open files, and remove the stream files
 * (unless -k). */
void clean_up(void)
{
    compile_abort();
    remove_streams(base_name);
}

/* Print the command-line syntax and exit. */
static void usage(void)
{
    fprintf(stderr, "usage: cccomp [-i] [-k] [-o output] input.c\n");
    exit(2);
}

/* Compile one .c file into the stream files. */
int main(int argc, char **argv)
{
    const char *src = NULL, *outname = NULL;
    int i;

    msg_init(argv[0]);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i"))
            opt_ir = 1;
        else if (!strcmp(argv[i], "-k"))
            opt_keep = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            outname = argv[++i];
        else if (argv[i][0] == '-' || src)
            usage();
        else
            src = argv[i];
    }
    if (!src)
        usage();
    if (!outname)
        outname = base_of(src);
    base_name = outname;
    compile_file(src, outname);
    return 0;
}
