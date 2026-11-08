/*
 * ccrel: ccish's final pass as a program of its own (approach.md,
 * "B3"), and its linker (issue 11). It reads the stream files that
 * ccgen wrote for one or more compiled files, its objects, and links
 * them into one module. The objects are kept, so that a changed file
 * needs only itself compiled again.
 *
 * Usage: ccrel [-l] [-o module] output|@file...
 *   output  an object's output name, as given to cccomp and ccgen; the
 *           objects are linked in this order. @file reads the names
 *           from file instead, one per line (CR or LF ends a line,
 *           and blank lines are skipped), since a link's names can
 *           overrun the VM's 127-character command line
 *           (doc/bootstrap-test.md, R10)
 *   -l      print a listing of the module's bytecode
 *   -o      module name (default: output, if there is only one)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "diag.h"
#include "link.h"

/* A partly written module, to remove if the final pass fails. */
static const char *module_name;

/* The objects' output names, from the command line or list files. */
static const char *bases[MAX_OBJS];
static int nbases;

/* The list files' names, each ended by a NUL. */
#define LIST_BYTES 512
static char list[LIST_BYTES];
static int list_len;

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
  fputs("usage: ccrel [-l] [-o module] output|@file...\n", stderr);
#else
  fputs("usage: ccrel [-o module] output|@file...\n", stderr);
#endif
  exit(2);
}

/* Add output name name to bases[]. */
static void add_base(const char *name)
{
  if (nbases == MAX_OBJS)
    fatal(223 /* "too many objects to link" */, NULL);
  bases[nbases++] = name;
}

/* Add the output names in list file path, one per line, to bases[]. */
static void read_list(const char *path)
{
  FILE *f = fopen(path, "rb");
  int c, at = list_len;         /* where this line's name starts */
  if (!f)
    fatal(171 /* "cannot open" */, path);
  do {
    c = getc(f);
    if (c != EOF && c != '\n' && c != '\r') {
      if (list_len >= LIST_BYTES - 1)   /* and room for its NUL */
        fatal(319 /* "names in the list file too long (512 bytes in all):" */,
              path);
      list[list_len++] = (char)c;
    } else if (list_len > at) {
      list[list_len++] = 0;
      add_base(list + at);
      at = list_len;
    }
  } while (c != EOF);
  fclose(f);
}

/* Link the objects into a module. */
int main(int argc, char **argv)
{
  const char *modname = NULL;
  int i;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
#ifdef CCISH_LISTING
    if (!strcmp(argv[i], "-l"))
      opt_list = 1;
    else
#endif
    if (!strcmp(argv[i], "-o") && i + 1 < argc)
      modname = argv[++i];
    else if (argv[i][0] == '-')
      usage();
    else if (argv[i][0] == '@')
      read_list(argv[i] + 1);
    else
      add_base(argv[i]);
  }
  if (nbases == 0 || (nbases > 1 && !modname))
    usage();
  if (!modname)
    modname = bases[0];
  module_name = modname;
  link_module(nbases, bases, modname);
  module_name = NULL;
  return 0;
}
