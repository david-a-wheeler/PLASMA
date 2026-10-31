/*
 * cccomp: ccish's front end as a program of its own (approach.md, "B3"
 * and 18.7). It parses one .c file into the IR file, NAME.ir, and
 * leaves it for ccgen, the back end, which writes the stream files for
 * ccrel. On the Apple II, a small driver runs cccomp, ccgen and then
 * ccrel, and each is freed when it ends, so each has nearly all of
 * memory.
 *
 * Usage: cccomp [-k] [-o output] input.c
 *   -k  keep the IR file even after an error
 *   -o  output name for the IR file (default: input name without ".c")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccish.h"

/* The IR file's output name, to remove it after an error. */
static const char *base_name;

/* After an error: close the open files, and remove the IR file (unless
 * -k). */
void clean_up(void)
{
  compile_abort();
  remove_stream(base_name, SUFFIX_IR);
}

/* Print the command-line syntax and exit. */
static void usage(void)
{
  fputs("usage: cccomp [-k] [-o output] input.c\n", stderr);
  exit(2);
}

/* Parse one .c file into the IR file. */
int main(int argc, char **argv)
{
  const char *src = NULL, *outname = NULL;
  int i;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-k"))
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
