/*
 * ccrel: ccish's final pass as a program of its own (approach.md,
 * "B3"). It reads the stream files that cccomp wrote, writes the
 * module, and then removes the stream files.
 *
 * Usage: ccrel [-l] [-k] [-o module] output
 *   output  the stream files' output name, as given to cccomp
 *   -l      print a listing of the module's bytecode
 *   -k      keep the stream files
 *   -o      module name (default: output)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ccish.h"

/* Files to remove if the final pass fails: the streams (unless -k) and
 * a partly written module. */
static const char *base_name;
static const char *module_name;

/* After an error: close the open files, and remove partial output. */
void clean_up(void)
{
  link_abort();
  remove_streams(base_name);
  if (module_name)
    remove(module_name);
}

/* Print the command-line syntax and exit. */
static void usage(void)
{
  fprintf(stderr, "usage: ccrel [-l] [-k] [-o module] output\n");
  exit(2);
}

/* Turn the stream files into a module. */
int main(int argc, char **argv)
{
  const char *base = NULL, *modname = NULL;
  int i;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-l"))
      opt_list = 1;
    else if (!strcmp(argv[i], "-k"))
      opt_keep = 1;
    else if (!strcmp(argv[i], "-o") && i + 1 < argc)
      modname = argv[++i];
    else if (argv[i][0] == '-' || base)
      usage();
    else
      base = argv[i];
  }
  if (!base)
    usage();
  if (!modname)
    modname = base;
  base_name = base;
  module_name = modname;
  link_module(base, modname);
  module_name = NULL;
  remove_streams(base);
  return 0;
}
