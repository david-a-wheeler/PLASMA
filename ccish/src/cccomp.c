// cccomp: ccish's front end as a program of its own (approach.md, "B3"
// and 18.7). It parses the token stream and name file that ccpp made of
// one .c file (NAME.i and NAME.nam, ../CCPP.md) into the IR file,
// NAME.ir, and leaves that for ccgen, the back end, which writes the
// stream files for ccrel. On the Apple II, a small driver runs ccpp,
// cccomp, ccgen and then ccrel, and each is freed when it ends, so each
// has nearly all of memory.
//
// Usage: cccomp [-k] [-o output] input
//   -k  keep the token stream, and keep the IR file even after an error
//   -o  output name for the IR file (default: the input name)
// It reads input.i and input.nam, and removes input.i once it succeeds
// (unless -k); ccgen needs input.nam too, so it removes that.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.opt.h"
#include "token.h"
#include "obj.h"
#include "ir.h"
#include "parse.h"

// The IR file's output name, to remove it after an error.
static const char *base_name;

// After an error: close the open files, and remove the IR file (unless
// -k).
void clean_up(void)
{
  compile_abort();
  remove_stream(base_name, SUFFIX_IR);
}

// Print the command-line syntax and exit.
static void usage(void)
{
  fputs("usage: cccomp [-k] [-o output] input\n", stderr);
  exit(2);
}

// Parse one token stream into the IR file.
int main(int argc, char **argv)
{
  const char *src = NULL, *outname = NULL;
  int i;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
    if (is_opt(argv[i], 'k'))
      opt_keep = 1;
    else if (is_opt(argv[i], 'o') && i + 1 < argc)
      outname = argv[++i];
    else if (argv[i][0] == '-' || src)
      usage();
    else
      src = argv[i];
  }
  if (!src)
    usage();
  if (!outname)
    outname = src;
  base_name = outname;
  compile_file(src, outname);
  remove_stream(src, SUFFIX_I);
  return 0;
}
