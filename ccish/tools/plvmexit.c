/* plvmexit.c: an exit() for programs run under plvm, such as ccish's
 * own passes, not part of the C library: lib/exit.c's is the Apple
 * II's, and can't unwind plvm (doc/bootstrap-test.md, R8). tools/cclink
 * links this one unless -a. It prints tools/ccstat.pla's marker with
 * the status, so tools/ccrun reports it, then runs a 6502 BRK, called
 * as a function (plvm runs a call to an address as 6502 code), and
 * plvm exits. Nothing unwinds, which is fine only because the process
 * ends. Don't use it on an Apple II: there a BRK drops into the
 * monitor, with the VM's stacks and the open files left as they were. */
#include <stdlib.h>

/* A BRK instruction: its opcode is 0. */
static char brk[1];

/* Stop plvm with status. */
void exit(int status)
{
  __putc(1);
  __puts(pstr("exit "));
  __puti(status & 255);
  __putln();
  ((void (*)(void))brk)();
}
