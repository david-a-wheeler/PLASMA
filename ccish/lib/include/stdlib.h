/* stdlib.h for ccish: exit is lib/exit.c's. */
#ifndef STDLIB_H
#define STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void exit(int status);
#endif
