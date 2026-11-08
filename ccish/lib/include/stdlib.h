/* stdlib.h for ccish. exit is declared but not defined yet: it has to
 * unwind the VM back to CMD (doc/bootstrap-test.md, R8), so linking a
 * call to it fails with "undefined reference to 'exit'". ccish's own
 * passes link tools/plvmexit.c's stand-in, for plvm only. */
#ifndef STDLIB_H
#define STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void exit(int status);
#endif
