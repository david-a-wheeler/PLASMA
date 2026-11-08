/* plvmexit.c: an exit() for running ccish's own passes under plvm, not
 * part of the C library (lib/ doesn't define exit yet; see
 * doc/bootstrap-test.md, R8). It prints tools/ccstat.pla's marker with
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
