// ccpp: ccish's preprocessor and lexer as a program of its own
// (approach.md, "ccpp design"). It turns one .c file into its token
// stream, NAME.i, and its name file, NAME.nam (../CCPP.md), for cccomp.
//
// Usage: ccpp [-E] [-k] [-o output] [-I dir] [-D name[=text]] [-U name]
//             input.c
//   -E  write the tokens as text to the standard output, as gcc -E
//       does, instead of NAME.i and NAME.nam (full build only)
//   -k  keep the output files even after an error
//   -o  output name (default: input name without ".c")
//   -I  a directory to look for #include files in, after the including
//       file's own for "file" (any number, in order)
//   -D  define name as text (as 1 if no text), before the source
//   -U  undefine name; -D and -U apply in the order given
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "diag.h"
#include "diag.opt.h"
#include "token.h"
#include "ppout.h"
#include "pp.h"

// The output name, to remove the outputs after an error.
static const char *base_name;

// After an error: close the open files, and remove the outputs (unless
// -k).
void clean_up(void)
{
  pp_abort();
  remove_stream(base_name, SUFFIX_I);
  remove_stream(base_name, SUFFIX_NAM);
}

// Print the command-line syntax and exit.
static void usage(void)
{
#ifdef CCISH_TEXT
  fputs("usage: ccpp [-E] [-k] [-o output] [-I dir] [-D name[=text]]"
        " [-U name] input.c\n", stderr);
#else
  fputs("usage: ccpp [-k] [-o output] [-I dir] [-D name[=text]]"
        " [-U name] input.c\n", stderr);
#endif
  exit(2);
}

// Preprocess one .c file.
int main(int argc, char **argv)
{
  const char *src = NULL, *outname = NULL;
  int i, j;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
#ifdef CCISH_TEXT
    if (is_opt(argv[i], 'e'))
      pp_opt_text = 1;
    else
#endif
    if (is_opt(argv[i], 'k'))
      opt_keep = 1;
    else if (is_opt(argv[i], 'o') && i + 1 < argc)
      outname = argv[++i];
    else if ((j = pp_arg(argc, argv, i)) >= 0)
      i = j;
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
  pp_file(src, outname);
  return 0;
}
