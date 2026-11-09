// ccgen: ccish's back end as a program of its own (approach.md 18.7).
// It reads the IR file, NAME.ir, that cccomp wrote, and the name file,
// NAME.nam, that ccpp wrote (for the names' spellings), lowers and
// optimizes the IR, and writes the stream file and symbol summary,
// NAME.obj and NAME.syms, for ccrel. The IR file and name file are
// removed afterward, unless -k; after an error, they are kept (for -i),
// and partial output removed.
//
// Usage: ccgen [-d] [-i] [-k] output
//   output  the output name given to cccomp
//   -d      print the machine ops after the peephole stage (full
//           build only: the listing needs a 12K buffer)
//   -i      print the IR as it is read
//   -k      keep the IR file and name file, and the stream files even
//           after an error
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "diag.h"
#include "token.h"
#include "ir.h"
#include "irin.h"

// The output name, to remove partial output after an error.
static const char *base_name;

// After an error: close the open files, and remove the stream files
// (unless -k).
void clean_up(void)
{
  gen_abort();
  remove_streams(base_name);
}

// Print the command-line syntax and exit.
static void usage(void)
{
#ifdef CCISH_LISTING
  fputs("usage: ccgen [-d] [-i] [-k] output\n", stderr);
#else
  fputs("usage: ccgen [-i] [-k] output\n", stderr);
#endif
  exit(2);
}

// Turn one IR file into the stream files.
int main(int argc, char **argv)
{
  int i;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
#ifdef CCISH_LISTING
    if (!strcmp(argv[i], "-d"))
      opt_dump = 1;
    else
#endif
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
  remove_stream(base_name, SUFFIX_NAM);
  return 0;
}
