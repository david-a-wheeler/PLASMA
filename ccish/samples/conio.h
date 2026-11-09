/* conio.h: ccish's view of CONIO's table of functions
 * (src/inc/conio.plh, src/libsrc/apple/conio.pla). Not standard C: it
 * uses ccish's import pragma (the guide, 11.1). Every CONIO function
 * returns one value, so each is declared to return an int. */
#ifndef CONIO_H
#define CONIO_H

#pragma plasma import CONIO
struct t_conio
{
  int (*keypressed)(void);
  int (*getkey)(void);
  int (*putchars)(int, char *);
  int (*clear)(int);
  int (*gotoxy)(int, int);
  int (*viewport)(int, int, int, int);
  int (*textctrl)(int, int);
  int (*textmode)(int);
  int (*grmode)(int);
  int (*grcolor)(int);
  int (*grplot)(int, int);
  int (*tone)(int, int);
  int (*rnd)(void);
};
extern struct t_conio conio;

#endif
