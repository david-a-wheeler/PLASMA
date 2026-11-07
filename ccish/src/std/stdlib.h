/* stdlib.h for ccish: the parts the compiler's source uses. The
 * host build gets libc's; a build by ccish itself (__CCISH__) gets
 * the ones in src/vmsys.c. */
#ifndef STDLIB_H
#define STDLIB_H

void exit(int code);
void abort(void);

#endif
