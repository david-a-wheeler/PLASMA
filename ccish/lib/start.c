/* start.c: the startup for main(argc, argv) (doc/bootstrap-test.md,
 * R7). When main has two parameters, cclink's init calls _start, which
 * splits CMDSYS's command line at spaces and calls main. argv[0] is the
 * module's name, without the "+" CMD's command line has on an Apple II
 * when CMD runs the module itself. */
#include <plasma.h>

#define MAX_ARGS 24         /* limit other: argv words; more are dropped */

int main(int argc, char **argv);

static char *args[MAX_ARGS + 1];

/* Split the command line into args[] and call main. The line is a
 * PLASMA string; moving its text down over its length byte makes room
 * for the last NUL without a second buffer. */
int _start(void)
{
  char *d = cmdsys.cmdline;
  char *s = d + 1;
  char *end = s + *d;
  int argc = 0;
  int gap = 1;
  if (s < end && *s == '+')
    s++;                    /* CMD's "+NAME" on an Apple II: NAME */
  while (s < end) {
    if (*s == ' ') {
      *d = 0;
      gap = 1;
    } else {
      if (gap && argc < MAX_ARGS)
        args[argc++] = d;
      gap = 0;
      *d = *s;
    }
    s++;
    d++;
  }
  *d = 0;
  return main(argc, args);
}
