/*
 * ccgen: ccish's back end as a program of its own (approach.md 18.7).
 * It reads the IR file, NAME.ir, that cccomp wrote, lowers and
 * optimizes it, and writes the stream file and symbol summary, NAME.obj
 * and NAME.syms, for ccrel. The IR file is removed afterward, unless
 * -k; after an error, it is kept (for -i), and partial output removed.
 *
 * Usage: ccgen [-i] [-k] output
 *   output  the output name given to cccomp
 *   -i      print the IR as it is read
 *   -k      keep the IR file, and the stream files even after an error
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccish.h"

/* The output name, to remove partial output after an error. */
static const char *base_name;

/* After an error: close the open files, and remove the stream files
 * (unless -k). */
void clean_up(void)
{
  gen_abort();
  remove_streams(base_name);
}

/* Print the command-line syntax and exit. */
static void usage(void)
{
  fputs("usage: ccgen [-i] [-k] output\n", stderr);
  exit(2);
}

/* Turn one IR file into the stream files. */
int main(int argc, char **argv)
{
  int i;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-i"))
      opt_ir = 1;
    else if (!strcmp(argv[i], "-k"))
      opt_keep = 1;
    else if (argv[i][0] == '-' || base_name)
      usage();
    else
      base_name = argv[i];
  }
  if (!base_name)
    usage();
  gen_file(base_name);
  remove_stream(base_name, SUFFIX_IR);
  return 0;
}
