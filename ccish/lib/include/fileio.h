/* fileio.h: ccish's view of FILEIO's table of functions
 * (src/inc/fileio.plh), for the library and for programs that want it.
 * Not standard C, and not for gcc: it uses ccish's import pragma (the
 * guide, 11.1). A path is a PLASMA string (a length byte, then the
 * text), and an error leaves its code in cmdsys.syserr, so this
 * includes <plasma.h>. geteof and getmark return two values, which the
 * guide (11.1) says to declare as a struct of two ints; they're plain
 * pointers here, untested, since plvm doesn't implement them. */
#ifndef FILEIO_H
#define FILEIO_H
#include <plasma.h>

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
