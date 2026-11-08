/* plasma.h: ccish's view of two PLASMA tables, for the library and for
 * programs that want them. Not standard C, and not for gcc: it uses
 * ccish's asm labels and its import pragma (the guide, 11.1).
 *
 * cmdsys is CMDSYS's own table (src/inc/cmdsys.plh): syspath and
 * cmdline are PLASMA strings (a length byte, then the text). fileio is
 * FILEIO's table of functions (src/inc/fileio.plh); a path is a PLASMA
 * string, and an error leaves its code in cmdsys.syserr. geteof and
 * getmark return two values, which the guide (11.1) says to declare as
 * a struct of two ints; they're plain pointers here, untested, since
 * plvm doesn't implement them. */
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

#pragma plasma import FILEIO
struct t_fileio
{
  int (*getpfx)(char *);
  int (*setpfx)(char *);
  int (*getfileinfo)(char *, char *);
  int (*setfileinfo)(char *, char *);
  void *geteof;
  int (*seteof)(int);
  int (*iobufalloc)(int);
  int (*open)(char *);
  int (*close)(int);
  int (*read)(int, char *, int);
  int (*write)(int, char *, int);
  int (*create)(char *, int, int);
  int (*destroy)(char *);
  int (*rename)(char *, char *);
  int (*newline)(int, int, int);
  int (*online)(int, char *);
  int (*readblock)(int, char *, int);
  int (*writeblock)(int, char *, int);
  void *getmark;
  int (*setmark)(int, int, int);
};
extern struct t_fileio fileio;

#endif
