/*
 * ccrel: ccish's final pass as a program of its own (approach.md,
 * "B3"), and its linker (issue 11). It reads the stream files that
 * cccomp wrote for one or more compiled files, its objects, and links
 * them into one module. The objects are kept, so that a changed file
 * needs only itself compiled again.
 *
 * Usage: ccrel [-l] [-o module] output...
 *   output  an object's output name, as given to cccomp; the objects
 *           are linked in this order
 *   -l      print a listing of the module's bytecode
 *   -o      module name (default: output, if there is only one)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccish.h"

/* A partly written module, to remove if the final pass fails. */
static const char *module_name;

/* The objects' output names, from the command line. */
static const char *bases[256];

/* After an error: close the open files, and remove partial output. */
void clean_up(void)
{
  link_abort();
  if (module_name)
    remove(module_name);
}

/* Print the command-line syntax and exit. */
static void usage(void)
{
#ifdef CCISH_LISTING
  fputs("usage: ccrel [-l] [-o module] output...\n", stderr);
#else
  fputs("usage: ccrel [-o module] output...\n", stderr);
#endif
  exit(2);
}

/* Link the objects into a module. */
int main(int argc, char **argv)
{
  const char *modname = NULL;
  int i, n = 0;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
#ifdef CCISH_LISTING
    if (!strcmp(argv[i], "-l"))
      opt_list = 1;
    else
#endif
    if (!strcmp(argv[i], "-o") && i + 1 < argc)
      modname = argv[++i];
    else if (argv[i][0] == '-' || n >= 256)
      usage();
    else
      bases[n++] = argv[i];
  }
  if (n == 0 || (n > 1 && !modname))
    usage();
  if (!modname)
    modname = bases[0];
  module_name = modname;
  link_module(n, bases, modname);
  module_name = NULL;
  return 0;
}
