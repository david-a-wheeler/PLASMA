/* plasma.h: ccish's view of CMDSYS's own table (src/inc/cmdsys.plh),
 * for the library and for programs that want it. Not standard C, and
 * not for gcc: it uses ccish's asm labels (the guide, 11.1). syspath
 * and cmdline are PLASMA strings (a length byte, then the text).
 * FILEIO's table is in <fileio.h>, apart, so that a program that needs
 * only this one (such as ccpp) spends none of its struct members on
 * FILEIO's. */
#ifndef PLASMA_H
#define PLASMA_H

struct t_cmdsys
{
  int sysver;
  char *syspath;
  char *cmdline;
  int (*modexec)(char *);
  int (*sysopen)(char *);
  int (*sysclose)(int);
  int (*sysread)(int, char *, int);
  int (*syswrite)(int, char *, int);
  char syserr;
};
extern struct t_cmdsys cmdsys __asm__("CMDSYS");

#endif
