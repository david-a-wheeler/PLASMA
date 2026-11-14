// cclink: ccish's final pass as a program of its own (approach.md,
// "B3"), and its linker (issue 11). It reads the stream files that
// ccgen wrote for one or more compiled files, its objects, and links
// them into one module. The objects are kept, so that a changed file
// needs only itself compiled again.
//
// Usage: cclink [-v] [-x] [-L dir] [-o module] output|@file...
//   output  an object's output name, as given to cccomp and ccgen; the
//           objects are linked in this order. @file reads the names
//           from file instead, one per line (CR or LF ends a line,
//           and blank lines are skipped), since a link's names can
//           overrun the VM's 127-character command line
//           (doc/bootstrap-test.md, R10). It's GCC's @file syntax,
//           but not its format: no options, quoting or white-space
//           splitting, which an Apple II's CR lines don't need
//   -v      print a listing of the module's bytecode
//   -L      a directory of the library's objects, which must have a
//           lib.map that says which object defines each name: the
//           objects that define the names the link wants, until none
//           remain, are added after the named ones (doc/linking.md,
//           section 5)
//   -o      module name (default: output, if there is only one)
//   -x      write an XLOAD image (../doc/xload.md, section 3),
//           not a REL module
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "diag.h"
#include "diag.opt.h"
#include "link.h"

// A partly written module, to remove if the final pass fails.
static const char *module_name;

// The objects' output names, from the command line or list files.
static const char *bases[MAX_OBJS];
static int nbases;

// The list files' names, each ended by a NUL.
#define LIST_BYTES 512
static char list[LIST_BYTES];
static int list_len;

// After an error: close the open files, and remove partial output.
void clean_up(void)
{
  link_abort();
  if (module_name)
    remove(module_name);
}

// Print the command-line syntax and exit.
static void usage(void)
{
#ifdef CCISH_LISTING
  fputs("usage: cclink [-v] [-x] [-L dir] [-o module] output|@file...\n",
        stderr);
#else
  fputs("usage: cclink [-x] [-L dir] [-o module] output|@file...\n",
        stderr);
#endif
  exit(2);
}

// Add output name name to bases[].
static void add_base(const char *name)
{
  if (nbases == MAX_OBJS)
    fatal0(223 /* "too many objects to link" */);
  bases[nbases++] = name;
}

// Add the output names in list file path, one per line, to bases[].
static void read_list(const char *path)
{
  FILE *f = fopen(path, "rb");
  int c, at = list_len;         // where this line's name starts
  if (!f)
    fatal_d(171 /* "cannot open" */, path);
  do {
    c = getc(f);
    if (c != EOF && c != '\n' && c != '\r') {
      if (list_len >= LIST_BYTES - 1)   // and room for its NUL
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

// Link the objects into a module.
int main(int argc, char **argv)
{
  const char *modname = NULL, *libdir = NULL;
  int i;

  msg_init(argv[0]);
  for (i = 1; i < argc; i++) {
#ifdef CCISH_LISTING
    if (is_opt(argv[i], 'v'))
      opt_list = 1;
    else
#endif
    if (is_opt(argv[i], 'o') && i + 1 < argc)
      modname = argv[++i];
    else if (is_opt(argv[i], 'x'))
      opt_xload = 1;
    else if (is_opt(argv[i], 'L') && i + 1 < argc)
      libdir = argv[++i];
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
  link_module(nbases, bases, modname, libdir);
  module_name = NULL;
  return 0;
}
